#include "greatest.h"
#include "../coro.h"

// test that basic yeald and local vars work...
TEST coro_basic()
{
    uint8_t stack[1024];
    coro co;
    co_init(&co, [](coro* co) {
        co_declare_locals(co,
            int cnt = 0;
        );
        co_begin(co);

        while((locals.cnt++) < 2)
        {
            co_yield(co);
        }

        co_end(co);
    }, stack, sizeof(stack));

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
    co_init(&co, [](coro* co) {
        static int cnt = 0;
        co_begin(co);

        while((cnt++) < 2)
        {
            co_yield(co);
        }

        co_end(co);
    }, nullptr, 0);

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
    co_init(&co, [](coro* co){
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
    }, stack, sizeof(stack));

    while(!co_completed(&co))
        co_resume(&co);

    ASSERT_EQ(2, coro_sub_call_loop);
    ASSERT_EQ(4, coro_sub_call_sub_loop);

    return 0;
}

// TEST coro_with_args()
// {
//     struct args
//     {
//         int  input;
//         int* output;
//     };
// 
//     int output = 7331;
// 
//     args a;
//     a.input = 1337;
//     a.output = &output;
// 
//     uint8_t stack[1024];
//     coro co;
//     co_init(&co, [](coro* co){
//         co_begin(co);
//         co_end(co);
//     }, stack, sizeof(stack), &a, sizeof(a), alignof(a));
// 
//     co_resume(&co);
//     ASSERT(co_completed(&co));
// }

GREATEST_SUITE( coro )
{
	RUN_TEST( coro_basic );
    RUN_TEST( coro_no_stack );
    RUN_TEST( coro_sub_call );
    // RUN_TEST( coro_with_args );
}

GREATEST_MAIN_DEFS();

int main( int argc, char **argv )
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE( coro );
    GREATEST_MAIN_END();
}
