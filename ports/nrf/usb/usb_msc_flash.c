/**************************************************************************/
/*!
    @file     usb_msc_flash.c
    @author   hathach (tinyusb.org)

    @section LICENSE

    Software License Agreement (BSD License)

    Copyright (c) 2018, Adafruit Industries (adafruit.com)
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holders nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/**************************************************************************/

#include "tusb.h"
#include "internal_flash.h"

// For updating fatfs's cache
#include "extmod/vfs.h"
#include "extmod/vfs_fat.h"
#include "lib/oofatfs/ff.h"
#include "py/mpstate.h"

/*------------------------------------------------------------------*/
/* MACRO TYPEDEF CONSTANT ENUM
 *------------------------------------------------------------------*/
#define MSC_FLASH_ADDR_END      0xED000
#define MSC_FLASH_SIZE          (256*1024)
#define MSC_FLASH_ADDR_START    (MSC_FLASH_ADDR_END-MSC_FLASH_SIZE)
#define MSC_FLASH_BLOCK_SIZE    512

#define FL_PAGE_SZ              4096

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb (uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize) {
    void const* resp = NULL;
    uint16_t len = 0;

    switch ( scsi_cmd[0] ) {
        case SCSI_CMD_TEST_UNIT_READY:
            // Command that host uses to check our readiness before sending other commands
            len = 0;
        break;

        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
            // Host is about to read/write etc ... better not to disconnect disk
            len = 0;
        break;

        case SCSI_CMD_START_STOP_UNIT:
            // Host try to eject/safe remove/poweroff us. We could safely disconnect with disk storage, or go into lower power
            // scsi_start_stop_unit_t const * cmd_start_stop = (scsi_start_stop_unit_t const *) scsi_cmd
            len = 0;
        break;

        // negative means error -> tusb could stall and/or response with failed status
        default: return -1;
    }

    // return len must not larger than bufsize
    if ( len > bufsize ) len = bufsize;

    // copy response to stack's buffer if any
    if ( resp && len ) {
        memcpy(buffer, resp, len);
    }

    return len;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb (uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    (void) lun;
    (void) offset;

    uint32_t const block_count = bufsize / MSC_FLASH_BLOCK_SIZE;

    internal_flash_read_blocks(buffer, lba, block_count);

    return block_count * MSC_FLASH_BLOCK_SIZE;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb (uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    (void) lun;
    (void) offset;

    uint32_t const block_count = bufsize / MSC_FLASH_BLOCK_SIZE;

    // bufsize <= CFG_TUD_MSC_BUFSIZE (4096)
    internal_flash_write_blocks(buffer, lba, block_count);

    // update fatfs's cache if address matches
    fs_user_mount_t* vfs = MP_STATE_VM(vfs_mount_table)->obj;

    if ( (lba <= vfs->fatfs.winsect) && (vfs->fatfs.winsect <= (lba + bufsize / MSC_FLASH_BLOCK_SIZE)) ) {
        memcpy(vfs->fatfs.win, buffer + MSC_FLASH_BLOCK_SIZE * (vfs->fatfs.winsect - lba), MSC_FLASH_BLOCK_SIZE);
    }

    return block_count * MSC_FLASH_BLOCK_SIZE;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void tud_msc_write10_complete_cb (uint8_t lun) {
    (void) lun;

    // flush pending cache when write10 is complete
    internal_flash_flush();
}
