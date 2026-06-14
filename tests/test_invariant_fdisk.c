#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * Security invariant:
 * When copying bootcode data into a fixed-size structure (mboot),
 * the copy_size MUST NEVER exceed sizeof(mboot). Any copy operation
 * that uses a size derived from external input (e.g., a file read)
 * must be bounded to prevent buffer overflow.
 *
 * This test simulates the vulnerable pattern and verifies that a
 * safe implementation correctly bounds the copy size.
 */

/* Simulate the mboot structure with a fixed size matching typical fdisk usage */
#define MBOOT_SIZE 512
#define BOOTCODE_MAGIC 0xAA55

typedef struct {
    uint8_t data[MBOOT_SIZE];
} mboot_t;

/*
 * Safe copy function that enforces the invariant:
 * copy_size must not exceed sizeof(mboot_t)
 */
static int safe_bootcode_copy(mboot_t *mboot, const uint8_t *bootcode, size_t copy_size) {
    if (mboot == NULL || bootcode == NULL) {
        return -1;
    }
    /* INVARIANT: copy_size must never exceed sizeof(mboot_t) */
    if (copy_size > sizeof(mboot_t)) {
        return -1; /* Reject oversized copy */
    }
    memcpy(mboot, bootcode, copy_size);
    return 0;
}

/*
 * Canary-protected wrapper to detect buffer overflows
 */
typedef struct {
    uint8_t canary_before[16];
    mboot_t mboot;
    uint8_t canary_after[16];
} protected_mboot_t;

static void init_canaries(protected_mboot_t *p) {
    memset(p->canary_before, 0xDE, sizeof(p->canary_before));
    memset(p->canary_after, 0xAD, sizeof(p->canary_after));
}

static int check_canaries(const protected_mboot_t *p) {
    for (size_t i = 0; i < sizeof(p->canary_before); i++) {
        if (p->canary_before[i] != 0xDE) return 0;
    }
    for (size_t i = 0; i < sizeof(p->canary_after); i++) {
        if (p->canary_after[i] != 0xAD) return 0;
    }
    return 1;
}

START_TEST(test_bootcode_copy_size_bounded)
{
    /* Invariant: copy_size derived from external input must never exceed sizeof(mboot_t) */

    /* Adversarial copy sizes that attempt to overflow the mboot buffer */
    size_t adversarial_sizes[] = {
        MBOOT_SIZE + 1,           /* One byte over */
        MBOOT_SIZE + 512,         /* Double the size */
        MBOOT_SIZE * 2,           /* Exactly double */
        4096,                     /* Typical page size */
        65536,                    /* Large value */
        SIZE_MAX,                 /* Maximum possible size_t */
        SIZE_MAX - 1,             /* Near maximum */
        SIZE_MAX / 2,             /* Half of maximum */
        0xFFFFFFFF,               /* 32-bit max */
        MBOOT_SIZE + 0x1000,      /* Offset attack */
        1024 * 1024,              /* 1 MB */
        1024 * 1024 * 10,         /* 10 MB */
        MBOOT_SIZE + sizeof(size_t), /* Pointer-sized overflow */
        MBOOT_SIZE + 16,          /* Small overflow */
        MBOOT_SIZE + 8,           /* Tiny overflow */
    };

    int num_sizes = sizeof(adversarial_sizes) / sizeof(adversarial_sizes[0]);

    for (int i = 0; i < num_sizes; i++) {
        size_t adversarial_size = adversarial_sizes[i];

        /* Allocate a bootcode buffer filled with adversarial data */
        /* Use a safe allocation size to avoid OOM, but track the intended size */
        size_t alloc_size = (adversarial_size > 1024 * 1024) ? 1024 * 1024 : adversarial_size;
        
        /* Skip if alloc_size is 0 or would cause issues */
        if (alloc_size == 0) continue;

        uint8_t *bootcode = malloc(alloc_size);
        if (bootcode == NULL) continue;

        /* Fill with adversarial pattern (shellcode-like) */
        memset(bootcode, 0x90, alloc_size); /* NOP sled */
        if (alloc_size >= 2) {
            bootcode[alloc_size - 2] = 0xAA;
            bootcode[alloc_size - 1] = 0x55;
        }

        /* Set up canary-protected mboot structure */
        protected_mboot_t protected;
        init_canaries(&protected);
        memset(&protected.mboot, 0, sizeof(mboot_t));

        /* Attempt the copy with adversarial size */
        int result = safe_bootcode_copy(&protected.mboot, bootcode, adversarial_size);

        /* INVARIANT 1: Oversized copies must be rejected */
        ck_assert_msg(result == -1,
            "SECURITY VIOLATION: copy_size=%zu (> MBOOT_SIZE=%d) was not rejected",
            adversarial_size, MBOOT_SIZE);

        /* INVARIANT 2: Canaries must remain intact (no buffer overflow occurred) */
        ck_assert_msg(check_canaries(&protected),
            "SECURITY VIOLATION: Buffer overflow detected - canaries corrupted for copy_size=%zu",
            adversarial_size);

        /* INVARIANT 3: mboot must not have been modified on rejection */
        uint8_t zero_mboot[MBOOT_SIZE];
        memset(zero_mboot, 0, sizeof(zero_mboot));
        ck_assert_msg(memcmp(&protected.mboot, zero_mboot, sizeof(mboot_t)) == 0,
            "SECURITY VIOLATION: mboot was modified despite rejection for copy_size=%zu",
            adversarial_size);

        free(bootcode);
    }
}
END_TEST

START_TEST(test_bootcode_copy_valid_sizes)
{
    /* Invariant: Valid copy sizes (<=MBOOT_SIZE) must succeed and not overflow */

    size_t valid_sizes[] = {
        0,
        1,
        MBOOT_SIZE / 2,
        MBOOT_SIZE - 1,
        MBOOT_SIZE,
        256,
        440,  /* MBR bootstrap code area */
        446,  /* MBR bootstrap code area (common) */
    };

    int num_sizes = sizeof(valid_sizes) / sizeof(valid_sizes[0]);

    for (int i = 0; i < num_sizes; i++) {
        size_t valid_size = valid_sizes[i];

        uint8_t bootcode[MBOOT_SIZE];
        memset(bootcode, 0x90, sizeof(bootcode));

        protected_mboot_t protected;
        init_canaries(&protected);
        memset(&protected.mboot, 0, sizeof(mboot_t));

        int result = safe_bootcode_copy(&protected.mboot, bootcode, valid_size);

        /* INVARIANT: Valid sizes must succeed */
        ck_assert_msg(result == 0,
            "Valid copy_size=%zu was incorrectly rejected", valid_size);

        /* INVARIANT: Canaries must remain intact even for valid copies */
        ck_assert_msg(check_canaries(&protected),
            "SECURITY VIOLATION: Canaries corrupted for valid copy_size=%zu", valid_size);

        /* INVARIANT: Copied data must match source */
        if (valid_size > 0) {
            ck_assert_msg(memcmp(&protected.mboot, bootcode, valid_size) == 0,
                "Data mismatch after valid copy of size=%zu", valid_size);
        }
    }
}
END_TEST

START_TEST(test_bootcode_copy_size_calculation)
{
    /*
     * Invariant: The copy_size calculation itself must be safe.
     * When copy_size = min(file_size, sizeof(mboot)), the result
     * must always be <= sizeof(mboot).
     */

    /* Simulate various "file sizes" read from disk */
    size_t file_sizes[] = {
        0,
        1,
        MBOOT_SIZE,
        MBOOT_SIZE + 1,
        MBOOT_SIZE * 2,
        65536,
        SIZE_MAX,
        SIZE_MAX - MBOOT_SIZE,
        0xDEADBEEF,
        0xCAFEBABE,
    };

    int num_sizes = sizeof(file_sizes) / sizeof(file_sizes[0]);

    for (int i = 0; i < num_sizes; i++) {
        size_t file_size = file_sizes[i];

        /* Safe copy_size calculation: bounded by sizeof(mboot_t) */
        size_t copy_size = (file_size < sizeof(mboot_t)) ? file_size : sizeof(mboot_t);

        /* INVARIANT: copy_size must never exceed sizeof(mboot_t) */
        ck_assert_msg(copy_size <= sizeof(mboot_t),
            "SECURITY VIOLATION: copy_size=%zu exceeds sizeof(mboot_t)=%zu for file_size=%zu",
            copy_size, sizeof(mboot_t), file_size);

        /* INVARIANT: copy_size must not wrap around (no integer overflow) */
        ck_assert_msg(copy_size <= file_size || file_size == 0,
            "SECURITY VIOLATION: copy_size=%zu > file_size=%zu (integer overflow?)",
            copy_size, file_size);
    }
}
END_TEST

START_TEST(test_null_pointer_safety)
{
    /* Invariant: NULL pointer inputs must be handled safely */
    uint8_t bootcode[MBOOT_SIZE];
    memset(bootcode, 0x41, sizeof(bootcode));

    mboot_t mboot;
    memset(&mboot, 0, sizeof(mboot));

    /* NULL mboot destination */
    int result = safe_bootcode_copy(NULL, bootcode, MBOOT_SIZE);
    ck_assert_msg(result == -1, "NULL mboot destination was not rejected");

    /* NULL bootcode source */
    result = safe_bootcode_copy(&mboot, NULL, MBOOT_SIZE);
    ck_assert_msg(result == -1, "NULL bootcode source was not rejected");

    /* Both NULL */
    result = safe_bootcode_copy(NULL, NULL, MBOOT_SIZE);
    ck_assert_msg(result == -1, "Both NULL pointers were not rejected");
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_bootcode_copy_size_bounded);
    tcase_add_test(tc_core, test_bootcode_copy_valid_sizes);
    tcase_add_test(tc_core, test_bootcode_copy_size_calculation);
    tcase_add_test(tc_core, test_null_pointer_safety);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}