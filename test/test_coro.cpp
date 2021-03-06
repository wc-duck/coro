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

#define CORO_TRACK_MAX_STACK_USAGE 0

#include "greatest.h"
#include "../coro.h"

// test that basic yeald and local vars work...
TEST coro_basic()
{
    uint8_t stack[1024];
    coro co;
    co_init(&co, stack, sizeof(stack), [](coro* co, void*, void*) {
        co_locals_begin(co);
            int cnt = 0;
        co_locals_end(co);

        co_begin(co);

        while((locals.cnt++) < 2)
        {
            co_yield(co);
        }

        co_end(co);
    });

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co, nullptr);

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co, nullptr);

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co, nullptr);

    ASSERT(co_completed(&co));

#if CORO_TRACK_MAX_STACK_USAGE
    printf("max stack %d\n", co.stack_use_max);
#endif

	return 0;
}

// test that coro work without locals...
TEST coro_no_stack()
{
    coro co;
    co_init(&co, nullptr, 0, [](coro* co, void*, void*) {
        static int cnt = 0;
        co_begin(co);

        while((cnt++) < 2)
        {
            co_yield(co);
        }

        co_end(co);
    });

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co, nullptr);

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co, nullptr);

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co, nullptr);

    ASSERT(co_completed(&co));

#if CORO_TRACK_MAX_STACK_USAGE
    printf("max stack %d\n", co.stack_use_max);
#endif

	return 0;
}

TEST coro_sub_call()
{
    struct test_state
    {
        int coro_sub_call_sub_loop = 0;
        int coro_sub_call_loop = 0;
    } state;

    uint8_t stack[1024];
    coro co;
    co_init(&co, stack, sizeof(stack), [](coro* co, void* userdata, void*){
        co_locals_begin(co);
            int cnt = 0;
        co_locals_end(co);

        co_begin(co);

        for(; locals.cnt < 2; ++locals.cnt)
        {
            ++((test_state*)userdata)->coro_sub_call_loop;
            co_call(co, [](coro* co, void* userdata, void*){
                co_locals_begin(co);
                    unsigned int cnt = 0;
                co_locals_end(co);

                co_begin(co);

                for(; locals.cnt < 2; ++locals.cnt)
                {
                    ++((test_state*)userdata)->coro_sub_call_sub_loop;
                    co_yield(co);
                }

                co_end(co);
            });
        }

        co_end(co);
    });

    while(!co_completed(&co))
        co_resume(&co, &state);

    ASSERT_EQ(2, state.coro_sub_call_loop);
    ASSERT_EQ(4, state.coro_sub_call_sub_loop);

#if CORO_TRACK_MAX_STACK_USAGE
    printf("max stack %d\n", co.stack_use_max);
#endif

    return 0;
}

TEST coro_with_args()
{
    struct args
    {
        int  input;
        int* output;
    };

    int output = 7331;

    args a;
    a.input = 1337;
    a.output = &output;

    uint8_t stack[1024];
    coro co;
    co_init(&co, stack, sizeof(stack), [](coro* co, void*, void* co_args) {
        args* arg = (args*)co_args;

        co_begin(co);

        *arg->output = arg->input;

        co_end(co);
    }, &a, sizeof(args), alignof(args));

    co_resume(&co, nullptr);
    ASSERT(co_completed(&co));

    ASSERT_EQ(1337, output);

#if CORO_TRACK_MAX_STACK_USAGE
    printf("max stack %d\n", co.stack_use_max);
#endif

    return 0;
}

TEST coro_with_args_in_subcall()
{
    int coro_with_args_in_subcall_sum = 0;

    uint8_t stack[1024];
    coro co;
    co_init(&co, stack, sizeof(stack), [](coro* co, void*, void*) {
        co_locals_begin(co);
            int cnt = 0;
        co_locals_end(co);

        co_begin(co);

        for(; locals.cnt < 2; ++locals.cnt)
        {
            co_call(co, [](coro* co, void* userdata, void* args){
                int* arg = (int*)args;

                co_begin(co);

                *((int*)userdata) += *arg + 10;

                co_end(co);
            }, &locals.cnt, sizeof(int), alignof(int));
        }

        co_end(co);
    });

    co_resume(&co, &coro_with_args_in_subcall_sum);
    ASSERT(co_completed(&co));

    ASSERT_EQ(21, coro_with_args_in_subcall_sum);

#if CORO_TRACK_MAX_STACK_USAGE
    printf("max stack %d\n", co.stack_use_max);
#endif

    return 0;
}

int coro_yield_without_braces()
{
    uint8_t stack[1024];
    coro co;
    co_init(&co, stack, sizeof(stack), [](coro* co, void*, void*) {
        co_locals_begin(co);
            int cnt = 0;
        co_locals_end(co);

        co_begin(co);

        while(true)
        {
            if(locals.cnt++ != 2)
                co_yield(co);
            else
                break;
        }

        co_end(co);
    });

    co_resume(&co, nullptr);
    ASSERT(!co_completed(&co));
    ASSERT(!co_waiting(&co));

    co_resume(&co, nullptr);
    ASSERT(!co_completed(&co));
    ASSERT(!co_waiting(&co));

    co_resume(&co, nullptr);
    ASSERT(co_completed(&co));
    ASSERT(!co_waiting(&co));

    return 0;
}

int coro_wait_without_braces()
{
    uint8_t stack[1024];
    coro co;
    co_init(&co, stack, sizeof(stack), [](coro* co, void*, void*) {
        co_locals_begin(co);
            int cnt = 0;
        co_locals_end(co);

        co_begin(co);

        while(true)
        {
            if(locals.cnt++ != 2)
                co_wait(co);
            else
                break;
        }

        co_end(co);
    });

    co_resume(&co, nullptr);
    ASSERT(!co_completed(&co));
    ASSERT(co_waiting(&co));

    co_resume(&co, nullptr);
    ASSERT(!co_completed(&co));
    ASSERT(co_waiting(&co));

    co_resume(&co, nullptr);
    ASSERT(co_completed(&co));
    ASSERT(!co_waiting(&co));

    return 0;
}

int coro_early_exit()
{
    coro co;
    co_init(&co, nullptr, 0, [](coro* co, void* userdata, void*) {
        int* test = (int*)userdata;

        co_begin(co);

        *test = 1; co_yield(co);
        *test = 2; co_yield(co);
        *test = 3; co_exit(co);
        *test = 4; co_yield(co);

        co_end(co);
    });

    int test = 0;

    while(!co_completed(&co))
        co_resume(&co, &test);

    ASSERT_EQ(3, test);
    return 0;
}

int coro_early_exit_without_braces()
{
    coro co;
    co_init(&co, nullptr, 0, [](coro* co, void* userdata, void*) {
        int* test = (int*)userdata;

        co_begin(co);

        while(true)
            if(++(*test) == 3)
                co_exit(co);

        co_end(co);
    });

    int test = 0;

    while(!co_completed(&co))
        co_resume(&co, &test);

    ASSERT_EQ(3, test);
    return 0;
}

static void alloc_140_bytes(coro* co, void*, void*)
{
    co_locals_begin(co);
        uint8_t dauta[140];
    co_locals_end(co);

    co_begin(co);
        for(int i = 0; i < (int)sizeof(locals.dauta); ++i)
            locals.dauta[i] = (uint8_t)i;

        co_yield(co);

        for(int i = 0; i < (int)sizeof(locals.dauta); ++i)
            assert(locals.dauta[i] == i);

    co_end(co);
}

int coro_stack_overflow_locals()
{
    uint8_t stack1[128];
    uint8_t stack2[256];

    coro co;
    co_init(&co, stack1, sizeof(stack1), alloc_140_bytes);

    co_resume(&co, nullptr);
    ASSERT(co_stack_overflowed(&co));

    ASSERT_EQ(stack1, co_replace_stack(&co, stack2, sizeof(stack2)));

    co_resume(&co, nullptr);
    ASSERT(!co_stack_overflowed(&co));
    ASSERT(!co_completed(&co));

    co_resume(&co, nullptr);
    ASSERT(!co_stack_overflowed(&co));
    ASSERT(co_completed(&co));

    return 0;
}

int coro_stack_overflow_locals_in_call()
{
    uint8_t stack1[128];
    uint8_t stack2[256];

    coro co;
    co_init(&co, stack1, sizeof(stack1), [](coro* co, void*, void*)
    {
        co_begin(co);
            co_call(co, alloc_140_bytes);
        co_end(co);

    });

    co_resume(&co, nullptr);
    ASSERT(co_stack_overflowed(&co));

    ASSERT_EQ(stack1, co_replace_stack(&co, stack2, sizeof(stack2)));

    co_resume(&co, nullptr);
    ASSERT(!co_stack_overflowed(&co));
    ASSERT(!co_completed(&co));

    co_resume(&co, nullptr);
    ASSERT(!co_stack_overflowed(&co));
    ASSERT(co_completed(&co));

    return 0;
}

int coro_stack_overflow_args_in_co_call()
{
    struct test_arg
    {
        uint8_t data[80];
    } the_arg;

    for(int i = 0; i < (int)sizeof(the_arg.data); ++i)
        the_arg.data[i] = (uint8_t)i;

    uint8_t stack1[128];
    uint8_t stack2[256];

    coro co;
    co_init(&co, stack1, sizeof(stack1), [](coro* co, void*, void* arg){
        test_arg* arg_ptr = (test_arg*)arg;
        co_begin(co);
            co_call(co, [](coro* co, void*, void* arg){
                test_arg* arg_ptr = (test_arg*)arg;
                co_begin(co);
                    for(int i = 0; i < (int)sizeof(arg_ptr->data); ++i)
                        assert(arg_ptr->data[i] == (uint8_t)i );
                co_end(co);
            }, *arg_ptr);
        co_end(co);
    }, the_arg);

    co_resume(&co, nullptr);
    ASSERT(co_stack_overflowed(&co));

    ASSERT_EQ(stack1, co_replace_stack(&co, stack2, sizeof(stack2)));

    co_resume(&co, nullptr);
    ASSERT(!co_stack_overflowed(&co));
    ASSERT(co_completed(&co));

    return 0;
}

static void empty_coro(coro* co, void*, void*)
{
    co_begin(co);
    co_end(co);
}

int coro_stack_overflow_call()
{
    static const size_t stack1_size = 128;
    uint8_t stack1[stack1_size];
    uint8_t stack2[stack1_size*2];

    coro co;
    co_init(&co, stack1, sizeof(stack1), [](coro* co, void*, void*){
        co_locals_begin(co);
            uint8_t data[stack1_size]; // filling the stack to the max!
        co_locals_end(co);

        co_begin(co);
            (void)locals;

            // this call should make stuff overflow!
            co_call(co, empty_coro);
        co_end(co);
    });

    co_resume(&co, nullptr);
    ASSERT(co_stack_overflowed(&co));
    ASSERT(!co_completed(&co));

    ASSERT_EQ(stack1, co_replace_stack(&co, stack2, sizeof(stack2)));

    co_resume(&co, nullptr);
    ASSERT(!co_stack_overflowed(&co));
    ASSERT(co_completed(&co));

    return 0;
}

int coro_stack_overflow_call_in_call()
{
    static const size_t stack1_size = 128;
    uint8_t stack1[stack1_size];
    uint8_t stack2[stack1_size*2];

    coro co;
    co_init(&co, stack1, sizeof(stack1), [](coro* co, void*, void*){
        co_begin(co);
            co_call(co, [](coro* co, void*, void*){
                co_locals_begin(co);
                    uint8_t data[stack1_size]; // filling the stack to the max!
                co_locals_end(co);

                co_begin(co);
                    (void)locals;

                    // this call should make stuff overflow!
                    co_call(co, empty_coro);
                co_end(co);
            });
        co_end(co);
    });

    co_resume(&co, nullptr);
    ASSERT(co_stack_overflowed(&co));
    ASSERT(!co_completed(&co));

    ASSERT_EQ(stack1, co_replace_stack(&co, stack2, sizeof(stack2)));

    co_resume(&co, nullptr);
    ASSERT(!co_stack_overflowed(&co));
    ASSERT(co_completed(&co));

    return 0;
}

GREATEST_SUITE( coro_tests )
{
	RUN_TEST( coro_basic );
    RUN_TEST( coro_no_stack );
    RUN_TEST( coro_sub_call );
    RUN_TEST( coro_with_args );
    RUN_TEST( coro_with_args_in_subcall );
    RUN_TEST( coro_yield_without_braces );
    RUN_TEST( coro_wait_without_braces );
    RUN_TEST( coro_early_exit );
    RUN_TEST( coro_early_exit_without_braces );
    RUN_TEST( coro_stack_overflow_locals );
    RUN_TEST( coro_stack_overflow_locals_in_call );
    RUN_TEST( coro_stack_overflow_args_in_co_call );
    RUN_TEST( coro_stack_overflow_call );
    RUN_TEST( coro_stack_overflow_call_in_call );
}

GREATEST_MAIN_DEFS();

int main( int argc, char **argv )
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE( coro_tests );
    GREATEST_MAIN_END();
}
