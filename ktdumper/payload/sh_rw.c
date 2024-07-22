#include <stdint.h>

void runner(void);

int main(void) {
    /* restore IRQ so that we don't enter here again */
    // *(uint32_t*)0x18 = 0xe59ff000;

    *(uint32_t*)0x20 = 0x60c02000;

    /* change fatal_err to jump to runner() */
    uint32_t *hook = (void*)0x60c048dc;
    *hook++ = 0xe51ff004;
    *hook++ = (uint32_t)runner;

    /* clean dcache line for hook */
    __asm__ volatile ("MCR p15, 0, %0, c7, c10, 1\n" :: "r" ((uint32_t)0x60c048dc) : "memory");
    __asm__ volatile ("MCR p15, 0, %0, c7, c10, 1\n" :: "r" ((uint32_t)0x60c048dc+4) : "memory");
    /* invalidate all icache */
    __asm__ volatile ("MCR p15, 0, %0, c7, c5, 0\n" :: "r" (0) : "memory");
}

volatile uint16_t *NAND_DATA = (void*)0x16000000;
volatile uint8_t *NAND_ADDR = (void*)0x16000010;
volatile uint8_t *NAND_CMD = (void*)0x16000020;

int nand_wait() {
	while (1) {
		uint16_t data;
		*NAND_CMD = 0x70;
		data = *NAND_DATA;
		if (data & 0x40) {
			return data;
		}
	}
}

int nand_read_mda(uint32_t page, void *dst) {
	uint8_t *cdst = dst;

	*NAND_CMD = 0x0A;
	*NAND_ADDR = 0x01; // num blocks lo
	*NAND_ADDR = 0x00; // num blocks hi
	*NAND_ADDR = page & 0xFF;
	*NAND_ADDR = (page >> 8) & 0xFF;
	*NAND_ADDR = (page >> 16) & 0xFF;
	*NAND_CMD = 0x30;

	int ret = nand_wait();

	*NAND_CMD = 0x00;

	for (int i = 0; i < 256 + 8; ++i) { // +16 bytes or 8 words for spare
		uint16_t data = *NAND_DATA;
		cdst[2 * i] = data & 0xFF;
		cdst[2 * i + 1] = data >> 8;
	}

    return ret;
}

int nand_read_sda(uint32_t page, void *dst) {
	uint8_t *cdst = dst;

	*NAND_CMD = 0x00;
	*NAND_ADDR = 0x01; // num blocks lo
	*NAND_ADDR = 0x00; // num blocks hi
	*NAND_ADDR = page & 0xFF;
	*NAND_ADDR = (page >> 8) & 0xFF;
	*NAND_ADDR = (page >> 16) & 0xFF;
	*NAND_CMD = 0x30;

	int ret = nand_wait();

	*NAND_CMD = 0x00;

	for (int i = 0; i < 256 + 8; ++i) { // +16 bytes or 8 words for spare
		uint16_t data = *NAND_DATA;
		cdst[2 * i] = data & 0xFF;
		cdst[2 * i + 1] = data >> 8;
	}

    return ret;
}

static void usb_drain(void) {
    /* drain the queue */
    volatile uint32_t* usb_queue = (void*)0x61100418;
    volatile uint32_t *hwreg = (void*)0x3a100008;
    void (*usb_run)() = (void*)0x60c02670;

    while (*usb_queue != 0) {
        *hwreg |= 0x200;
        usb_run();
    }
}

void runner(void) {
    int (*usb_getch)() = (void*)0x60c03fa8;
    int (*usb_send)() = (void*)0x60c041ac;
    int (*usb_send_commit)() = (void*)0x60c03b28;

    while (1) {
        uint8_t ch = usb_getch();
        if (ch == 0x42) {
            /* handshake */
            uint8_t resp = 0x43;
            usb_send(&resp, 1);
            usb_send_commit();
        } else if (ch == 0x10 || ch == 0x11 || ch == 0x12) {
            /* read */
            uint8_t addrb[4];
            for (int i = 0; i < 4; ++i)
                addrb[i] = usb_getch();

            uint32_t addr = addrb[0] | (addrb[1] << 8) | (addrb[2] << 16) | (addrb[3] << 24);
            uint32_t resp, resplen;

            switch (ch) {
            case 0x10:
                resp = *(volatile uint8_t*)addr;
                resplen = 1;
                break;
            case 0x11:
                resp = *(volatile uint16_t*)addr;
                resplen = 2;
                break;
            case 0x12:
                resp = *(volatile uint32_t*)addr;
                resplen = 4;
                break;
            }

            usb_send(&resp, resplen);
            usb_send_commit();
        } else if (ch == 0x20 || ch == 0x21 || ch == 0x22) {
            /* write */
            uint8_t addrb[4], datab[4] = { 0 };
            for (int i = 0; i < 4; ++i)
                addrb[i] = usb_getch();

            uint32_t addr = addrb[0] | (addrb[1] << 8) | (addrb[2] << 16) | (addrb[3] << 24);

            int wlen = 1 << (ch & 0xF);
            for (int i = 0; i < wlen; ++i)
                datab[i] = usb_getch();

            uint32_t data = datab[0] | (datab[1] << 8) | (datab[2] << 16) | (datab[3] << 24);

            switch (wlen) {
            case 1:
                *(volatile uint8_t*)addr = data;
                break;
            case 2:
                *(volatile uint16_t*)addr = data;
                break;
            case 4:
                *(volatile uint32_t*)addr = data;
                break;
            }
        } else if (ch == 0x50) {
            /* nand_read_mda */
            uint8_t pageb[4];
            for (int i = 0; i < 4; ++i)
                pageb[i] = usb_getch();
            uint32_t page = pageb[0] | (pageb[1] << 8) | (pageb[2] << 16) | (pageb[3] << 24);

            uint8_t buf[528];
            uint8_t ret = nand_read_mda(page, buf);
            usb_send(&ret, 1);
            usb_send(&buf[0], 264);
            usb_send_commit();
            usb_getch();
            usb_send(&buf[264], 264);
            usb_send_commit();
        } else if (ch == 0x51) {
            /* nand_read_sda */
            uint8_t pageb[4];
            for (int i = 0; i < 4; ++i)
                pageb[i] = usb_getch();
            uint32_t page = pageb[0] | (pageb[1] << 8) | (pageb[2] << 16) | (pageb[3] << 24);

            uint8_t buf[528];
            uint8_t ret = nand_read_sda(page, buf);
            usb_send(&ret, 1);
            usb_send(&buf[0], 264);
            usb_send_commit();
            usb_getch();
            usb_send(&buf[264], 264);
            usb_send_commit();
        }
    }
}

__asm__(
".section .text.start\n"
".global start\n"
"start:\n"
    // prepare space for chainloaded PC
"    sub sp, #4\n"
"    stmdb sp!,{r0,r1,r2,r3,r4,r5,r12,lr}\n"

    // clean data cache and flush icache before jumping to rest of payload
    // hopefully increase stability bc we only need 1-2 cache lines to hit
"    ldr r0, =0xE55B0000\n"
"    ldr r1, =0xE55B1000\n"
"loop:\n"
"    mcr p15, 0, r0, c7, c10, 1\n"
"    add r0, r0, #32\n"
"    cmp r0, r1\n"
"    bne loop\n"
"    mov r0, #0\n"
"    mcr p15, 0, r0, c7, c5, 0\n"

"    bl main\n"

    // chainload to the real usb interrupt
"    mov r0, #0x20\n"
"    ldr r0, [r0]\n"
"    str r0, [sp,#32]\n"

"    ldmia sp!,{r0,r1,r2,r3,r4,r5,r12,lr,pc}\n"
);
