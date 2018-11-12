#pragma once

#include <stdio.h> // TODO: remove, used for printf!
#include <stdint.h>
#include <new>

// config
#if !defined(CORO_LOCALS_NAME)
#  define CORO_LOCALS_NAME locals
#endif

#if !defined(CORO_ASSERT)
#  include <assert.h>
#  define CORO_ASSERT(cond, msg) assert((cond) && msg);
#endif

/**
 *
 */
typedef void(*co_func)(struct coro*);

/**
 *
 */
enum coro_state
{
    CORO_STATE_CREATED,
    CORO_STATE_RUNNING,
    CORO_STATE_COMPLETED
};

/**
 *
 */
struct coro
{
    co_func    func         {nullptr};
    int        state        {0};
    coro_state run_state    {CORO_STATE_CREATED};

    int        stack_size   {0};
    uint8_t*   stack_top    {nullptr};
    uint8_t*   stack        {nullptr};

    coro*      sub_call     {nullptr};
    void*      call_args    {nullptr};
};

/**
 * 
 */
static inline void co_init( coro*     co,
                            uint8_t*  stack,
                            int       stack_size,
                            co_func   func );

/**
 *
 */
static inline void co_init( coro*     co,
                            uint8_t*  stack,
                            int       stack_size,
                            co_func   func,
                            void*     arg,
                            int       arg_size,
                            int       arg_align );

/**
 * 
 */
static inline void co_resume( coro* co );

/**
 * 
 */
static inline bool co_completed( coro* co ) { return co->run_state == CORO_STATE_COMPLETED; }

/**
 * 
 */
static inline void* co_args( coro* co );

/**
 *
 */
#define co_begin(co)

/**
 *
 */
#define co_end(co)

/**
 *
 */
#define co_yield(co)

/**
 *
 */
#define co_call(co, to_call)

/**
 * 
 */
#define co_declare_locals(co, locals)

#undef co_begin
#undef co_end
#undef co_yield
#undef co_call
#undef co_declare_locals

static inline uint8_t* _co_align_up(uint8_t* ptr, size_t align)
{
    return (uint8_t*)( ( (uintptr_t)ptr + ( (uintptr_t)align - 1 ) ) & ~( (uintptr_t)align - 1 ) );    
}

static inline void* _co_stack_alloc(coro* co, size_t size, size_t align)
{
    // does it fit?
    uint8_t* ptr = _co_align_up(co->stack_top, align);

    co->stack_top = ptr + size;

    CORO_ASSERT(co->stack_top <= co->stack + co->stack_size, "Stack overflow in coro!");

    return ptr;
}

static inline void _co_stack_rewind(coro* co, void* ptr)
{
    CORO_ASSERT(ptr >= co->stack && ptr < co->stack + co->stack_size, "ptr from outside current coro-stack is used for rewind of stack!!!");
    co->stack_top = (uint8_t*)ptr;
}

static inline void co_init( coro*    co,
                            uint8_t* stack,
                            int      stack_size,
                            co_func  func,
                            void*    arg,
                            int      arg_size,
                            int      arg_align )
{
    co->func      = func;
    co->state     = 0;
    co->run_state = CORO_STATE_CREATED;

    co->stack      = stack;
    co->stack_top  = stack;
    co->stack_size = stack_size;

    co->sub_call   = nullptr;
    co->call_args  = nullptr;

    if(arg)
    {
        co->call_args = _co_stack_alloc(co, (size_t)arg_size, (size_t)arg_align);
        memcpy(co->call_args, arg, (size_t)arg_size);
    }
}

static inline void co_init( coro*    co,
                            uint8_t* stack,
                            int      stack_size,
                            co_func  func )
{
    co_init( co, stack, stack_size, func, nullptr, 0, 0 );
}

template<typename T>
static inline void co_init( coro* co, uint8_t* stack, int stack_size, co_func func, T& arg )
{
    co_init( co, stack, stack_size, func, &arg, sizeof(T), alignof(T) );
}

static inline void co_resume(coro* co)
{
    co->func(co);
}

static inline void* co_args(coro* co)
{
    CORO_ASSERT(co->call_args != nullptr, "requesting args in a coro called without args!");
    return co->call_args;
}

static inline bool _co_sub_call(coro* co)
{
    if(co->sub_call != nullptr)
    {
        co_resume(co->sub_call);
        if(co_completed(co->sub_call))
        {
            _co_stack_rewind(co, co->sub_call);
            co->sub_call = nullptr;
        }
    }
    return co->sub_call != nullptr;
}

#define co_begin(co)    \
    if(_co_sub_call(co)) \
        return;         \
    switch(co->state)   \
    {                   \
        default:        \
            co->run_state = CORO_STATE_RUNNING;

#define co_end(co) \
    }              \
    co->run_state = CORO_STATE_COMPLETED

#define co_yield(co) \
    do{ co->state = __LINE__; return; } while(0); case __LINE__:

/**
 *
 */
#define co_wait()

static inline bool _co_call(coro* co, co_func to_call, void* arg, int arg_size, int arg_align )
{
    co->sub_call = (coro*)_co_stack_alloc(co, sizeof(coro), alignof(coro));
    co_init(co->sub_call, co->stack_top, (int)(co->stack_size - (co->stack_top - co->stack)), to_call, arg, arg_size, arg_align);
    return _co_sub_call(co);
}

static inline bool _co_call(coro* co, co_func to_call)
{
   return _co_call(co, to_call, nullptr, 0, 0);
}

#define co_call(co, to_call, ...) \
    if(_co_call(co, to_call, ##__VA_ARGS__)) \
        { co_yield(co); }

#define co_declare_locals(co, locals)                          \
    struct _co_locals                                          \
    {                                                          \
        locals                                                 \
    };                                                         \
    _co_locals& CORO_LOCALS_NAME = *(_co_locals*)_co_align_up(co->stack, alignof(_co_locals)); \
    if(co->run_state == CORO_STATE_CREATED)                    \
    {                                                          \
        _co_stack_alloc(co, sizeof(_co_locals), alignof(_co_locals)); \
        new (&CORO_LOCALS_NAME) _co_locals;                          \
    }
