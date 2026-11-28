#ifndef PI5LA_TEST_STUB_PIOLIB_H
#define PI5LA_TEST_STUB_PIOLIB_H

/*
 * Host stand-in for piolib.h (raspberrypi/utils), for unit tests only.
 *
 * src/record.h and src/capture.h include piolib.h for the PIO handle type.
 * test_record compiles src/record.c and src/shipper.c and stubs
 * capture_stream() itself, so no piolib function is ever called; only the
 * type is needed. With this header first on the include path, test_record
 * builds on any Linux machine without piolib installed.
 *
 * The typedef matches piolib's own: PIO is a pointer to an opaque instance.
 * Do not add function declarations here. A test that needs piolib behaviour
 * must stub it in the test file, the way test_record stubs capture_stream().
 */
typedef struct pio_instance *PIO;

#endif
