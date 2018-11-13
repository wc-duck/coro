/*
   Header implementing "protothreads" but with a stack to support
   local-varible state, argument-passing and sub-coroutines.

   version 1.0, november, 2018

   Copyright (C) 2018- Fredrik Kihlander

   https://github.com/wc-duck/coro

   This software is provided 'as-is', without any express or implied
   warranty.  In no event will the authors be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
      claim that you wrote the original software. If you use this software
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.
   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original software.
   3. This notice may not be removed or altered from any source distribution.

   Fredrik Kihlander
*/

/**
 * About and usage here.
 * 
 * -- one section about waits and co_wait() --
 */

#pragma once

#include <stdint.h>
#include <new>


////////////////////////////////////////////////////////////////
//                           CONFIG                           //
////////////////////////////////////////////////////////////////

/**
 * Define CORO_LOCALS_NAME to configure name of variable declared
 * by co_declare_locals()
 * Defaults to 'locals'
 */
#if !defined(CORO_LOCALS_NAME)
#  define CORO_LOCALS_NAME locals
#endif

/**
 * Define to override how asserts are implemented, defaults to using
 * standard assert() from <assert.h>
 */
#if !defined(CORO_ASSERT)
#  include <assert.h>
#  define CORO_ASSERT(cond, msg) assert((cond) && msg);
#endif


////////////////////////////////////////////////////////////////
//                         PUBLIC API                         //
////////////////////////////////////////////////////////////////

/**
 * Signature used by all coroutine-callbacks.
 */
typedef void(*co_func)(struct coro*);

/**
 * State of coroutine.
 */
enum coro_state
{
    CORO_STATE_CREATED,   ///< Coroutine has been initialized but has never been co_resumed().
    CORO_STATE_RUNNING,   ///< Coroutine is running.
    CORO_STATE_COMPLETED  ///< Coroutine has completed, calling co_resume() on this is invalid!
};

/**
 * Struct keeping state for one coroutine.
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
 * Initialize coroutine. This will not call the coroutine-function, that will be done by
 * co_resume().
 * There is no need for the coroutine to have a stack but a stack is required to use
 * arguments and co_call().
 * 
 * @note arguments are copied via memcpy so data used as argument need to support that.
 * 
 * @note stack-overflow is only handled via an CORO_ASSERT().
 * 
 * @param co coroutine to initialize.
 * @param stack ptr to memory-segment to use as stack, can be null.
 * @param stack_size size of memory-region pointed to by stack, if stack == null this should be 0.
 * @param func coroutine callback.
 * @param arg optional pointer to argument to pass to coroutine, fetch in callback with co_arg().
 * @param arg_size size of data pointed to by arg.
 * @param arg_align alignment-requirement for data pointed to by arg.
 */
static inline void co_init( coro*   co,
                            void*   stack,
                            int     stack_size,
                            co_func func,
                            void*   arg,
                            int     arg_size,
                            int     arg_align );

/**
 * Initialize coroutine without argument.
 * @see co_init() for doc.
 */
static inline void co_init( coro*   co,
                            void*   stack,
                            int     stack_size,
                            co_func func );

/**
 * Initialize coroutine with argument.
 * @see co_init() for doc.
 */
template<typename T>
static inline void co_init( coro* co, void* stack, int stack_size, co_func func, T& arg );

/**
 * Resume execution of coroutine, this will run the coroutine until it yields or
 * exits.
 * 
 * yielding is done by calling co_yeald(), co_wait(), co_call() if the called coro
 * does not return on its first co_resume().
 * 
 * @note it is invalid to call co_resume() on a completed coroutine.
 */
static inline void co_resume( coro* co );


/**
 * Returns true if the coroutine has completed.
 */
static inline bool co_completed( coro* co ) { return co->run_state == CORO_STATE_COMPLETED; }

/**
 * Returns a pointer to arguments passed to co_init(). This pointer will need to be fetched
 * at each invocation of the coroutine-callback but will be persistent between co_resume()-calls.
 * I.e. it is valid to modify an argument in a coroutine and expect that modification to persist
 * between calls.
 * 
 * @note It is required to call this BEFORE co_begin().
 */
static inline void* co_arg( coro* co );

/**
 * Begin coroutine, the system expects a matching co_begin()/co_end() pair in a co_func.
 * 
 * @note if a co_func uses co_arg() or co_declare_locals() these are required to be called BEFORE
 *       co_begin().
 */
#define co_begin(co)

/**
 * Begin coroutine, the system expects a matching co_begin()/co_end() pair in a co_func.
 */
#define co_end(co)

/**
 * Yield execution of coroutine, coroutine will be continued after co_yeald() at the next co_resume()
 */
#define co_yield(co)

/**
 *
 */
#define co_wait(co)

/**
 * Perform a sub-call of another coroutine from current coroutine.
 * If coroutine returns without yielding this call will not yeald however if the called function
 * yields this will also yield and continue from after co_call() when the sub-call yields.
 * 
 * the sub-call will be resumed whenever the "top-level" coroutine is co_resumed(), i.e. the user
 * will only require to co_resume() the toplevel coroutine.
 * 
 * Can be called in 3 different ways
 * 
 * // only call coro_callback as coroutine.
 * co_call(co, coro_callback);
 *
 * // only call coro_callback with argument.
 * int my_arg = 0;
 * co_call(co, coro_callback, &my_arg, sizeof(int), alignof(int));
 * 
 * // if compiling as c++ you can just pass the argument.
 * int my_arg = 0;
 * co_call(co, coro_callback, my_arg);
 */
#define co_call(co, to_call, ...)

/**
 * Declare variables "local" to the coroutine that will be persisted between calls to co_resume()
 * for this specific coroutine.
 * The "local" variables will be stored in a variable named via the define CORO_LOCALS_NAME that
 * default to "locals".
 * 
 * @note It is required to call this BEFORE co_begin().
 * 
 * @example
 * 
 * void my_coroutine( coro* co )
 * {
 *    co_declare_locals(co,
 *      int my_local_int = 1;
 *      float my_local_float = 13.37f;
 *    );
 * 
 *    co_begin();
 * 
 *    use_int_and_float( locals.my_local_int, locals.my_local_float );
 * 
 *    co_end();
 * }
 * 
 * @note ADD NOTE ABOUT IMPLEMENTATION to know the limitations.
 */
#define co_declare_locals(co, locals)




////////////////////////////////////////////////////////////////
//                       IMPLEMENTATION                       //
////////////////////////////////////////////////////////////////

#undef co_begin
#undef co_end
#undef co_yield
#undef co_wait
#undef co_call
#undef co_declare_locals

static inline uint8_t* _co_align_up(uint8_t* ptr, size_t align)
{
    return (uint8_t*)( ( (uintptr_t)ptr + ( (uintptr_t)align - 1 ) ) & ~( (uintptr_t)align - 1 ) );    
}

static inline void* _co_stack_alloc(coro* co, size_t size, size_t align)
{
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

static inline void co_init( coro*   co,
                            void*   stack,
                            int     stack_size,
                            co_func func,
                            void*   arg,
                            int     arg_size,
                            int     arg_align )
{
    co->func      = func;
    co->state     = 0;
    co->run_state = CORO_STATE_CREATED;

    co->stack      = (uint8_t*)stack;
    co->stack_top  = (uint8_t*)stack;
    co->stack_size = stack_size;

    co->sub_call   = nullptr;
    co->call_args  = nullptr;

    if(arg)
    {
        CORO_ASSERT(stack != nullptr, "can't have arguments to a coroutine without a stack!");
        co->call_args = _co_stack_alloc(co, (size_t)arg_size, (size_t)arg_align);
        memcpy(co->call_args, arg, (size_t)arg_size);
    }
}

static inline void co_init( coro*   co,
                            void*   stack,
                            int     stack_size,
                            co_func func )
{
    co_init( co, stack, stack_size, func, nullptr, 0, 0 );
}

template<typename T>
static inline void co_init( coro* co, void* stack, int stack_size, co_func func, T& arg )
{
    co_init( co, stack, stack_size, func, &arg, sizeof(T), alignof(T) );
}

static inline void co_resume(coro* co)
{
    CORO_ASSERT(!co_completed(co), "can't resume a completed coroutine!");
    co->func(co);
}

static inline void* co_arg(coro* co)
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

#define co_wait(co)

static inline bool _co_call(coro* co, co_func to_call, void* arg, int arg_size, int arg_align )
{
    co->sub_call = (coro*)_co_stack_alloc(co, sizeof(coro), alignof(coro));
    co_init(co->sub_call, co->stack_top, (int)(co->stack_size - (co->stack_top - co->stack)), to_call, arg, arg_size, arg_align);
    return _co_sub_call(co);
}

template< typename T >
static inline bool _co_call(coro* co, co_func to_call, T& arg )
{
    co->sub_call = (coro*)_co_stack_alloc(co, sizeof(coro), alignof(coro));
    co_init(co->sub_call, co->stack_top, (int)(co->stack_size - (co->stack_top - co->stack)), to_call, &arg, sizeof(T), alignof(T));
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
