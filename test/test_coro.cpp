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

#include "greatest.h"
#include "../coro.h"

// test that basic yeald and local vars work...
TEST coro_basic()
{
    uint8_t stack[1024];
    coro co;
    co_init(&co, stack, sizeof(stack), [](coro* co) {
        co_declare_locals(co,
            int cnt = 0;
        );
        co_begin(co);

        while((locals.cnt++) < 2)
        {
            co_yield(co);
        }

        co_end(co);
    });

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co);

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co);

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co);

    ASSERT(co_completed(&co));

	return 0;
}

// test that coro work without locals...
TEST coro_no_stack()
{
    coro co;
    co_init(&co, nullptr, 0, [](coro* co) {
        static int cnt = 0;
        co_begin(co);

        while((cnt++) < 2)
        {
            co_yield(co);
        }

        co_end(co);
    });

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co);

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co);

    ASSERT_FALSE(co_completed(&co));
    co_resume(&co);

    ASSERT(co_completed(&co));

	return 0;
}

int coro_sub_call_sub_loop = 0;
int coro_sub_call_loop = 0;

TEST coro_sub_call()
{
    coro_sub_call_sub_loop = 0;
    coro_sub_call_loop = 0;

    uint8_t stack[1024];
    coro co;
    co_init(&co, stack, sizeof(stack), [](coro* co){
        co_declare_locals(co,
            int cnt = 0;
        );
        co_begin(co);

        for(; locals.cnt < 2; ++locals.cnt)
        {
            ++coro_sub_call_loop;
            co_call(co, [](coro* co){
                co_declare_locals(co,
                    unsigned int cnt = 0;
                );

                co_begin(co);

                for(; locals.cnt < 2; ++locals.cnt)
                {
                    ++coro_sub_call_sub_loop;
                    co_yield(co);
                }

                co_end(co);
            });
        }

        co_end(co);
    });

    while(!co_completed(&co))
        co_resume(&co);

    ASSERT_EQ(2, coro_sub_call_loop);
    ASSERT_EQ(4, coro_sub_call_sub_loop);

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
    co_init(&co, stack, sizeof(stack), [](coro* co) {
        args* arg = (args*)co_arg(co);

        co_begin(co);

        *arg->output = arg->input;

        co_end(co);
    }, &a, sizeof(args), alignof(args));

    co_resume(&co);
    ASSERT(co_completed(&co));

    ASSERT_EQ(1337, output);

    return 0;
}

int coro_with_args_in_subcall_sum = 0;

TEST coro_with_args_in_subcall()
{
    coro_with_args_in_subcall_sum = 0;

    uint8_t stack[1024];
    coro co;
    co_init(&co, stack, sizeof(stack), [](coro* co) {
        co_declare_locals(co,
            int cnt = 0;
        );
        co_begin(co);

        for(; locals.cnt < 2; ++locals.cnt)
        {
            ++coro_sub_call_loop;
            co_call(co, [](coro* co){
                int* arg = (int*)co_arg(co);

                co_begin(co);

                coro_with_args_in_subcall_sum += *arg + 10;

                co_end(co);
            }, &locals.cnt, sizeof(int), alignof(int));
        }

        co_end(co);
    });

    co_resume(&co);
    ASSERT(co_completed(&co));

    ASSERT_EQ(21, coro_with_args_in_subcall_sum);

    return 0;
}

GREATEST_SUITE( coro_tests )
{
	RUN_TEST( coro_basic );
    RUN_TEST( coro_no_stack );
    RUN_TEST( coro_sub_call );
    RUN_TEST( coro_with_args );
    RUN_TEST( coro_with_args_in_subcall );
}

GREATEST_MAIN_DEFS();

int main( int argc, char **argv )
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE( coro_tests );
    GREATEST_MAIN_END();
}
