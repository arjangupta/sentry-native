#include "sentry_envelope.h"
#include "sentry_path.h"
#include "sentry_string.h"
#include "sentry_testsupport.h"
#include "transports/sentry_function_transport.h"
#include <sentry.h>

typedef struct {
    uint64_t called;
    sentry_stringbuilder_t serialized_envelope;
} sentry_attachments_testdata_t;

static void
send_envelope(sentry_envelope_t *envelope, void *_data)
{
    sentry_attachments_testdata_t *data = _data;
    data->called += 1;
    sentry__envelope_serialize_into_stringbuilder(
        envelope, &data->serialized_envelope);
}

#ifdef __ANDROID__
#    define PREFIX "/data/local/tmp/"
#else
#    define PREFIX ""
#endif

SENTRY_TEST(lazy_attachments)
{
    sentry_attachments_testdata_t testdata;
    testdata.called = 0;
    sentry__stringbuilder_init(&testdata.serialized_envelope);

    sentry_options_t *options = sentry_options_new();
    sentry_options_set_dsn(options, "https://foo@sentry.invalid/42");
    sentry_options_set_transport(
        options, sentry_new_function_transport(send_envelope, &testdata));
    sentry_options_set_release(options, "prod");

    sentry_options_add_attachment(
        options, "existing-attachment", PREFIX ".existing-file-attachment");
    sentry_options_add_attachment(options, "non-existing-attachment",
        PREFIX ".non-existing-file-attachment");
    sentry_path_t *existing
        = sentry__path_from_str(PREFIX ".existing-file-attachment");
    sentry_path_t *non_existing
        = sentry__path_from_str(PREFIX ".non-existing-file-attachment");

    sentry_init(options);

    sentry__path_write_buffer(existing, "foo", 3);
    sentry_capture_event(sentry_value_new_message_event(
        SENTRY_LEVEL_INFO, "root", "Hello World!"));

    char *serialized
        = sentry_stringbuilder_take_string(&testdata.serialized_envelope);
    TEST_CHECK(strstr(serialized,
                   "{\"type\":\"attachment\",\"length\":3,\"name\":\"existing-"
                   "attachment\",\"filename\":\".existing-file-attachment\"}"
                   "\nfoo\n")
        != NULL);
    TEST_CHECK(
        strstr(serialized, "\"name\":\"non-existing-attachment\"") == NULL);
    sentry_free(serialized);

    sentry__path_write_buffer(existing, "foobar", 6);
    sentry__path_write_buffer(non_existing, "it exists", 9);
    sentry_capture_event(sentry_value_new_message_event(
        SENTRY_LEVEL_INFO, "root", "Hello World!"));

    serialized
        = sentry_stringbuilder_take_string(&testdata.serialized_envelope);
    TEST_CHECK(strstr(serialized,
                   "{\"type\":\"attachment\",\"length\":6,\"name\":\"existing-"
                   "attachment\",\"filename\":\".existing-file-attachment\"}"
                   "\nfoobar\n")
        != NULL);
    TEST_CHECK(strstr(serialized,
                   "{\"type\":\"attachment\",\"length\":9,\"name\":\"non-"
                   "existing-attachment\",\"filename\":\".non-existing-file-"
                   "attachment\"}"
                   "\nit exists\n")
        != NULL);
    sentry_free(serialized);

    sentry_shutdown();

    sentry__path_remove_all(existing);
    sentry__path_remove_all(non_existing);
    sentry__path_free(existing);
    sentry__path_free(non_existing);

    TEST_CHECK_INT_EQUAL(testdata.called, 2);
}
