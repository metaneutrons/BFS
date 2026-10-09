/* SPDX-License-Identifier: MPL-2.0 */
/*
 * Guest side of the keyboard-reset commit test.
 *
 * Appends numbered 16-byte records to DH1:reset-records under the default
 * delayed commit policy and records the last sequence number the volume
 * accepted in SYS:reset-progress, a file on the host-visible system drive.
 * It runs until the machine resets; the host then checks the image.
 */
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#define RECORD_SIZE 16

static void format_record(char record[RECORD_SIZE], ULONG sequence)
{
    record[0] = 'R'; record[1] = 'E'; record[2] = 'C'; record[3] = ' ';
    for (int i = 11; i >= 4; i--) {
        record[i] = "0123456789abcdef"[sequence & 15];
        sequence >>= 4;
    }
    record[12] = ' '; record[13] = 'o'; record[14] = 'k'; record[15] = '\n';
}

int main(void)
{
    BPTR records = Open("DH1:reset-records", MODE_NEWFILE);
    BPTR progress = Open("SYS:reset-progress", MODE_NEWFILE);
    if (!records || !progress) return 20;
    char record[RECORD_SIZE];
    for (ULONG sequence = 0;; sequence++) {
        format_record(record, sequence);
        if (Write(records, record, RECORD_SIZE) != RECORD_SIZE) return 20;
        /* Only a record whose Write returned is reported. */
        if (Seek(progress, 0, OFFSET_BEGINNING) < 0 ||
            Write(progress, record, RECORD_SIZE) != RECORD_SIZE)
            return 20;
    }
}
