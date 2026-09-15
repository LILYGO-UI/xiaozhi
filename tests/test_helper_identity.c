#define _GNU_SOURCE
#include <assert.h>
#include <string.h>

#define main xiaozhi_helper_main
#include "../src/protocol/xiaozhi_helper.c"
#undef main

static void assert_uuid_v4(const char *uuid)
{
    assert(strlen(uuid) == 36);
    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) assert(uuid[i] == '-');
        else assert((uuid[i] >= '0' && uuid[i] <= '9') || (uuid[i] >= 'a' && uuid[i] <= 'f'));
    }
    assert(uuid[14] == '4');
    assert(strchr("89ab", uuid[19]));
}

int main(void)
{
    char first[37];
    char again[37];
    // Regression for Linux: raw 32-digit machine-id was rejected
    // by the OTA service, while this stable UUID identifies activation and sessions.
    make_client_id("00112233445546778899aabbccddeeff", first, sizeof(first));
    assert(strcmp(first, "00112233-4455-4677-8899-aabbccddeeff") == 0);
    make_client_id("00112233445546778899AABBCCDDEEFF", again, sizeof(again));
    assert(strcmp(first, again) == 0);
    assert_uuid_v4(first);

    make_client_id("0123456789abcdef0123456789abcdef", first, sizeof(first));
    assert(strcmp(first, "01234567-89ab-4def-8123-456789abcdef") == 0);
    make_client_id("0123456789abcdef0123456789abcdef", again, sizeof(again));
    assert(strcmp(first, again) == 0);
    assert_uuid_v4(first);

    const char *invalid[] = {NULL, "", "uninitialized", "1234", "00112233445546778899aabbccddeefx",
                             "00112233445546778899aabbccddeeff00"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        make_client_id(invalid[i], first, sizeof(first));
        assert_uuid_v4(first);
    }

    // Defaults must never rewrite explicit caller identities. CLI takes priority.
    app_opts_t opts;
    char *environment_args[] = {"helper", "probe", NULL};
    setenv("XIAOZHI_CLIENT_ID", "explicit-environment-client", 1);
    assert(parse_args(2, environment_args, &opts) == 0);
    assert(strcmp(opts.client_id, "explicit-environment-client") == 0);
    char *command_args[] = {"helper", "probe", "--client-id", "explicit-command-client", NULL};
    assert(parse_args(4, command_args, &opts) == 0);
    assert(strcmp(opts.client_id, "explicit-command-client") == 0);
    unsetenv("XIAOZHI_CLIENT_ID");
    return 0;
}
