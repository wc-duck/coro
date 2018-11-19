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

/*
    Small example showing how to implement a really simple "wait"-system ontop of 
    'coro'.
    As there is only waiting for timeout here, no more advanced things than this is needed.
*/

#include "../coro.h"
#include <stdio.h>

#if defined(WIN64) || defined(_WIN64) || defined(WIN32) || defined(_WIN32)
#else
#include <unistd.h> // usleep()
#endif

#include <stdlib.h> // rand()

// Define our own co_sleep_ms() that will suspend the thread for x milliseconds.
#define co_sleep_ms(co, ms) \
    do { sleep_time = (ms); co_wait(co); } while(0)

static void print_char(char c)
{
    putc(c, stdout);
    fflush(stdout);
}

static unsigned int sleep_time = 0;

void print_line(coro* co, void*, const char** args)
{
    // all values are returned as a pointer to the type and a line to print
    // was passed as argument here.
    const char* line = *args;

    // declare loacal state to keep track of what char to print next.
    co_declare_locals(co,
        // init to 8... it just so happens that name + indent is 8 chars ;)
        int curr_char = 8;
    );

    // mark the begining of the coro-functions executing code. This is required
    // by the 'coro' system... and must be matched by a co_end().
    co_begin(co);

    printf("%.8s", line);

    while(line[locals.curr_char] != '\0')
    {
        print_char(line[locals.curr_char++]);

        // suspend the coroutine and have the outside system wait for x milliseconds!
        co_sleep_ms(co, 30 + rand() % 150);
    }
    print_char('\n');
    co_end(co);
}

struct print_dialog_arg
{
    const char** lines;
    size_t       line_cnt;
};

void print_dialog(coro* co, void*, print_dialog_arg* args)
{
    // declare locals to keep track of current line.
    co_declare_locals(co,
        size_t curr_line = 0;
    );

    co_begin(co);

    while(locals.curr_line != args->line_cnt)
    {
        // for each line, call a sub-coro-function with the line to print as argument.
        co_call(co, (co_func)print_line, args->lines[locals.curr_line++]);

        // sleep coroutine for x milliseconds between each line.
        co_sleep_ms(co, 500 + rand() % 200);
    }

    co_end(co);
}

int main(int, const char**)
{
    uint8_t* stack[512];

    static const char* LINES[] = {
            "Bob     Yo alice. I heard you like mudkips.",
			"Alice   No Bob. Not me. Who told you such a thing?",
			"Bob     Alice please, don't lie to me. We've known each other a long time.",
			"Alice   We have grown apart. I barely know myself.",
			"Bob     OK.",
			"Alice   Good bye Bob. I wish you the best.",
			"Bob     But do you like mudkips?",
			"Alice   <has left>",
			"Bob     Well, I like mudkips :)"
    };

    print_dialog_arg dialog_args {
        LINES,
        sizeof(LINES) / sizeof(const char*)
    };

    coro co;
    co_init(&co, stack, sizeof(stack), (co_func)print_dialog, dialog_args);

    // run until the top-level coroutine has completed... i.e. all lines was printed.
    while(!co_completed(&co))
    {
        // resume will resume execution where the coroutine left of!
        co_resume(&co, nullptr);

        // since we only have one system that can wait() our coroutines in this example,
        // and that is sleep, sleep if waiting ;)
        if(co_waiting(&co))
        {
            #if defined(WIN64) || defined(_WIN64) || defined(WIN32) || defined(_WIN32)
                SleepEx( sleep_time, false );
            #else
                usleep( sleep_time * 1000 );
            #endif
        }
    }

    return 0;
}