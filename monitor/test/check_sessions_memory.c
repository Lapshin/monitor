//
// Created by Nikita Litvinov on 07.08.18.
//

#include <string.h>
#include <errno.h>

#include <fff.h>
#include <tap.h>

#include <database.c>

DEFINE_FFF_GLOBALS


void check_sessions_memory_test(void)
{
    int i = 0, k;
    int ret = check_sessions_memory(-1);
    ok(ret == -1, "Check not valid fd");

    for(i = 1; i < ALLOC_SESSION_MAX_FD + 2; i++)
    {
        ret = check_sessions_memory(i);
        ok(ret == 0, "Valid fd passed");
        ok(i == session_max_fd, "session_max_fd_test");
    }
    k = i - 1;
    for(i = k; i > 2; i--)
    {
        ret = check_sessions_memory(i);
        ok(ret == 0, "Valid fd passed");
        ok(k == session_max_fd, "session_max_fd_test");
    }
}


int main(int argc, char *argv[])
{
    plan(NO_PLAN);
    check_sessions_memory_test();
    done_testing();
}
