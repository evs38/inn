/* $Id$ */
/* concat test suite. */

#include <stdio.h>
#include <string.h>

#include "libinn.h"

#define END     (char *) 0

static void
ok(int n, int success)
{
    printf("%sok %d\n", success ? "" : "not ", n);
}

int
main(void)
{
    printf("11\n");
    ok( 1, !strcmp("a",     concat("a",                   END)));
    ok( 2, !strcmp("ab",    concat("a", "b",              END)));
    ok( 3, !strcmp("ab",    concat("ab", "",              END)));
    ok( 4, !strcmp("ab",    concat("", "ab",              END)));
    ok( 5, !strcmp("",      concat("",                    END)));
    ok( 6, !strcmp("abcde", concat("ab", "c", "", "de",   END)));
    ok( 7, !strcmp("abcde", concat("abc", "de", END, "f", END)));
    ok( 8, !strcmp("/foo",             concatpath("/bar", "/foo")));
    ok( 9, !strcmp("/foo/bar",         concatpath("/foo", "bar")));
    ok(10, !strcmp("./bar",            concatpath("/foo", "./bar")));
    ok(11, !strcmp("/bar/baz/foo/bar", concatpath("/bar/baz", "foo/bar")));
    return 0;
}
