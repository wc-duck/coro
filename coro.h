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
 * This is a small header/library implementing coroutines/protothreads/"yieldable functions"
 * or whatever you want to call it. Initial idea comest from here:
 * 
 * https://www.chiark.greenend.org.uk/~sgtatham/coroutines.html
 * 
 * 
 * BASICS:
 * 
 * To make a function yieldable that function will need to be a 'co_func' and contain
 * a co_begin()/co_end(co)-pair. I.e.
 * 
 * void my_coroutine(coro* co, void*, void*)
 * {
 *     co_begin(co);
 * 
 *     co_end(co);
 * }
 * 
 * And to run that one instance of struct coro need to be created either on stack or
 * dynamically. To then run the function use co_resume() and check for completion with
 * co_completed()
 * 
 * void run_it()
 * {
 *     coro co;
 *     co_init(&co, nullptr, 0, my_coroutine);
 * 
 *     while(!co_completed(&co))
 *         co_resume(&co, nullptr);
 * }
 * 
 * 
 * YIELD-POINTS:
 * 
 * "yield-points" are points in a coroutine where execution might yield and continue from
 * on the next call to co_resume().
 * Yield-points are introduced by co_yield(), co_wait() and co_call().
 * 
 * 
 * LOCAL VARIABLES:
 * 
 * Local varibles in coroutine-functions are a bit tricky since the coroutine callback 
 * will be called over and over again, all local variables will be re-initialized at each
 * entry. The solution here is to use co_locals_begin()/co_locals_end(). But for this to 
 * work a stack need to be accociated with the coroutine.
 * This stack can be allocated in any way as long as it is valid for the entire runtime of
 * the coroutine.
 * void my_coroutine(coro* co, void*, void*)
 * {
 *     co_locals_begin(co);
 *         int var1 = 0;
 *         float var2 = 13.37f;
 *     co_locals_end(co);
 * 
 *     co_begin(co);
 * 
 *     // use locals.var1 or locals.var2
 * 
 *     co_end(co);
 * }
 * 
 * void run_it()
 * {
 *     uint8_t stack[256];
 *     coro co;
 *     co_init(&co, stack, sizeof(stack), my_coroutine);
 * 
 *     while(!co_completed(&co))
 *         co_resume(&co, nullptr);
 * }
 * 
 * the locals declared will be pointing to the same position in the stack at each call
 * so these will be modifiable by the coroutine at will.
 * 
 * 
 * CALLING OTHER YIELDABLE FUNCTIONS:
 * 
 * When providing a stack to the coroutine you can also call other yieldable functions
 * from your coroutine with co_call().
 * 
* void my_sub_coroutine(coro* co, void*, void*)
 * {
 *     co_begin(co);
 * 
 *     // ... do stuff here ...
 * 
 *     co_end(co);
 * }
 * 
 * void my_coroutine(coro* co, void*, void*)
 * {
 *     co_begin(co);
 * 
 *     co_call(co, my_sub_coroutine);
 * 
 *     co_end(co);
 * }
 * 
 * The coroutine called by co_call() can now yield etc and is now the part of the code
 * that controlls the state of the stack of sub-calls until it exits.
 * 
 * 
 * ARGUMENTS:
 * 
 * both co_init() and co_call() supports arguments as well, these arguments will be passed
 * on the stack via the last argument to co_func.
 * 
 * The arguments will be residing on the stack until the called function has completed so
 * it is valid to write to arguments if needed.
 * 
 * Observe that arguments are copied with memcpy and no destructors are run on them!
 * 
 * 
 * COROUTINES AND WAITS:
 * In many cases you might want coroutines that are 'waiting', i.e. suspended until some
 * event occurs, such as a timeout or operation-x completed.
 * 
 * However since these waits and updates of coroutines are outside the scope of this lib
 * and nothing I can/want to dictate how it should work it will only provide one function 
 * to help out with that and let the user build something around it with this lib as a 
 * building-block.
 * That function is co_wait(). co_wait() is basically a co_yield() but it will flag the
 * coroutine and all its parent-coroutines and 'waiting' ( to be checked with co_waiting() ).
 * This flag will be cleared at the next call to co_resume().
 */

#pragma once

#include <stdint.h>
#include <string.h> // memcpy
#include <new>


////////////////////////////////////////////////////////////////
//                           CONFIG                           //
////////////////////////////////////////////////////////////////

/**
 * Define CORO_LOCALS_NAME to configure name of variable declared
 * by co_locals_begin()/co_locals_end()
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

/**
 * If defined to 1 struct coro will have an extra member called stack_use_max
 * that will be the maximum amount of stack that has been used by the coro during
 * it's lifetime, default to 0
 */
#if !defined(CORO_TRACK_MAX_STACK_USAGE)
#  define CORO_TRACK_MAX_STACK_USAGE 0
#endif


////////////////////////////////////////////////////////////////
//                         PUBLIC API                         //
////////////////////////////////////////////////////////////////

/**
 * Signature used by all coroutine-callbacks.
 * 
 * These functions must follow this pattern
 * 
 * void my_func(coro* co, void* userdata, void* arg )
 * {
 *     // start with declaring locals, this is optional
 *     co_locals_begin(co)
 *     // ...
 *     co_locals_end(co)
 * 
 *     co_begin(co); // required!
 * 
 *     // ... code for coroutine goes here! ...
 * 
 *     co_end(co); // required!
 * }
 * 
 * @param co state of current coroutine call, use with all other co_**** functions/macros.
 * @param userdata passed to co_resume() at the top-level.
 * @param arg argument passed to co_init() or co_call(), data will be preserved between calls
 *            and can thus be modified etc. Will be nullptr if no argument was passed.
 */
typedef void(*co_func)(struct coro* co, void* userdata, void* arg);

/**
 * State of coroutine.
 */
enum
{
    CORO_STATE_CREATED   = -1, ///< Coroutine has been initialized but has never been co_resumed().
    CORO_STATE_COMPLETED = -2  ///< Coroutine has completed, calling co_resume() on this is invalid!
};

/**
 * Internal state used to keep the state of one call to a coroutine-callback.
 * Extracted to keep stack-state only in root-'coro' this to save stack-space
 * and make stack-replacement simpler.
 * 
 * @note this will be cast to coro* in sub-calls to be able to call a co_func
 *       with both co_resume() and co_call().
 */
struct _coro_call_state
{
    struct coro* root;        ///< pointer to the top-level (and only) coro*.
    co_func      func;
    int32_t      state;
    uint32_t     sub_call;
    uint32_t     call_locals;
    uint32_t     call_args;
};

/**
 * Struct keeping state for one coroutine.
 * 
 * TODO: this struct is bigger than it need to be, replace pointers with offset on stack
 *       instead or just store bool for sub_call and size of locals and args and "dig" into
 *       the stack.
 *       Making this smaller would mean less stackusage as coro:s for sub-calls are placed
 *       on the stack.
 */
struct coro
{
    _coro_call_state call;

    uint32_t   waiting : 1;

    int        stack_size   {0};
    uint8_t*   stack_top    {nullptr};
    uint8_t*   stack        {nullptr};
    void*      userdata     {nullptr};

#if CORO_TRACK_MAX_STACK_USAGE
    int        stack_use_max {0};
#endif
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
 * @param arg optional pointer to argument to pass to coroutine as the second argument in callback.
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
 * 
 * @param userdata passed to all invocation of co_func in coro.
 */
static inline void co_resume( coro* co, void* userdata );


/**
 * Returns true if the coroutine has completed.
 */
static inline bool co_completed( coro* co ) { return co->call.state == CORO_STATE_COMPLETED; }

/**
 * Returns true if the coroutine or any sub-coroutine has yielded via co_wait()
 */
static inline bool co_waiting( coro* co ) { return co->waiting == 1; }

/**
 * Begin coroutine, the system expects a matching co_begin()/co_end() pair in a co_func.
 * 
 * @note if a co_func uses co_locals_begin()/co_locals_end() this is required to be called 
 *       BEFORE co_begin().
 */
#define co_begin(co)

/**
 * End coroutine, the system expects a matching co_begin()/co_end() pair in a co_func.
 */
#define co_end(co)

/**
 * Early termiation of the current coroutine. This will be the same as the function
 * reaching co_end()
 */
#define co_exit(co)

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
 *    co_locals_begin(co);
 *      int my_local_int = 1;
 *      float my_local_float = 13.37f;
 *    co_locals_end(co);
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
#define co_locals_begin(co)
#define co_locals_end(co)




////////////////////////////////////////////////////////////////
//                       IMPLEMENTATION                       //
////////////////////////////////////////////////////////////////

#undef co_begin
#undef co_end
#undef co_exit
#undef co_yield
#undef co_wait
#undef co_call
#undef co_locals_begin
#undef co_locals_end

static inline void* _co_stack_alloc(_coro_call_state* call, size_t size, size_t align)
{
    coro* co = call->root;

    // align up!
    uint8_t* ptr = (uint8_t*)( ( (uintptr_t)co->stack_top + ( (uintptr_t)align - 1 ) ) & ~( (uintptr_t)align - 1 ) );

    co->stack_top = ptr + size;

    CORO_ASSERT(co->stack_top <= co->stack + co->stack_size, "Stack overflow in coro!");

#if CORO_TRACK_MAX_STACK_USAGE
    int stack_use = (int)(co->stack_top - co->stack);
    co->stack_use_max = stack_use > co->stack_use_max ? stack_use : co->stack_use_max;
#endif

    return ptr;
}

static inline void _co_stack_rewind(_coro_call_state* call, void* ptr)
{
    coro* co = call->root;
    CORO_ASSERT(ptr >= co->stack && ptr < co->stack + co->stack_size, "ptr from outside current coro-stack is used for rewind of stack!!!");
    co->stack_top = (uint8_t*)ptr;
}

static inline uint32_t _co_ptr_to_stack_offset(_coro_call_state* call, void* ptr)
{
    return (uint32_t)((uint8_t*)ptr - call->root->stack);
}

static inline void* _co_stack_offset_to_ptr(_coro_call_state* call, uint32_t offset)
{
    return call->root->stack + offset;
}

static inline void _co_init_call_state( _coro_call_state* call,
                                        coro*             root,
                                        co_func           func,
                                        void*             arg,
                                        int               arg_size,
                                        int               arg_align )
{
    call->state       = 0;
    call->root        = root;
    call->func        = func;
    call->sub_call    = 0xFFFFFFFF;
    call->call_locals = 0xFFFFFFFF;
    call->call_args   = 0xFFFFFFFF;
    if(arg)
    {
        CORO_ASSERT(call->root->stack != nullptr, "can't have arguments to a coroutine without a stack!");
        void* call_args = _co_stack_alloc(call, (size_t)arg_size, (size_t)arg_align);
        memcpy(call_args, arg, (size_t)arg_size);
        call->call_args = _co_ptr_to_stack_offset(call, call_args);
    }
}

static inline void co_init( coro*   co,
                            void*   stack,
                            int     stack_size,
                            co_func func,
                            void*   arg,
                            int     arg_size,
                            int     arg_align )
{
    co->waiting    = 0;
    co->stack      = (uint8_t*)stack;
    co->stack_top  = (uint8_t*)stack;
    co->stack_size = stack_size;
    co->userdata    = nullptr;

#if CORO_TRACK_MAX_STACK_USAGE
    co->stack_use_max = 0;
#endif

    _co_init_call_state(&co->call, co, func, arg, arg_size, arg_align);
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

static inline void _co_invoke_callback(_coro_call_state* call)
{
    call->func((coro*)call, call->root->userdata, _co_stack_offset_to_ptr(call, call->call_args));
}

static inline void co_resume(coro* co, void* userdata)
{
    CORO_ASSERT(!co_completed(co), "can't resume a completed coroutine!");
    co->waiting = 0;
    co->userdata = userdata;
    _co_invoke_callback(&co->call);
    co->userdata = nullptr;
}

static inline bool _co_sub_call(_coro_call_state* call)
{
    if(call->sub_call != 0xFFFFFFFF)
    {
        _coro_call_state* sub_call = (_coro_call_state*)_co_stack_offset_to_ptr(call, call->sub_call);
        _co_invoke_callback(sub_call);

        if(co_completed((coro*)sub_call))
        {
            _co_stack_rewind(call, sub_call);
            call->sub_call = 0xFFFFFFFF;
        }
    }
    return call->sub_call != 0xFFFFFFFF;
}

#define co_begin(co)            \
    if(_co_sub_call(&co->call)) \
        return;                 \
    switch(co->call.state)      \
    {                           \
        default:

#define co_exit(co) \
    do{ co->call.state = CORO_STATE_COMPLETED; return; } while(0)

#define co_end(co) \
    }              \
    co_exit(co)

#define co_yield(co) \
    do { co->call.state = __LINE__; return; case __LINE__: {} } while(0)

#define co_wait(co) \
    do { co->call.root->waiting = 1; co_yield(co); } while(0)

static inline bool _co_call(coro* co, co_func to_call, void* arg, int arg_size, int arg_align )
{
    _coro_call_state* sub_call = (_coro_call_state*)_co_stack_alloc(&co->call, sizeof(_coro_call_state), alignof(_coro_call_state));
    _co_init_call_state(sub_call, co->call.root, to_call, arg, arg_size, arg_align);
    co->call.sub_call = _co_ptr_to_stack_offset(&co->call, sub_call);
    return _co_sub_call(&co->call);
}

template< typename T >
static inline bool _co_call(coro* co, co_func to_call, T& arg )
{
    return _co_call(co, to_call, &arg, sizeof(T), alignof(T));
}

static inline bool _co_call(coro* co, co_func to_call)
{
   return _co_call(co, to_call, nullptr, 0, 0);
}

#define co_call(co, to_call, ...)            \
    if(_co_call(co, to_call, ##__VA_ARGS__)) \
        co_yield(co);

#define co_locals_begin(co) \
    struct _co_locals       \
    {

#define co_locals_end(co)                                                       \
    };                                                                          \
    if(co->call.call_locals == 0xFFFFFFFF)                                      \
    {                                                                           \
        void* call_locals = _co_stack_alloc( &co->call,                         \
                                             sizeof(_co_locals),                \
                                             alignof(_co_locals));              \
        new (call_locals) _co_locals;                                           \
        co->call.call_locals = _co_ptr_to_stack_offset(&co->call, call_locals); \
    }                                                                           \
    _co_locals& CORO_LOCALS_NAME = *((_co_locals*)_co_stack_offset_to_ptr(&co->call, co->call.call_locals)); \

