/*
 * Copyright (c) 2011, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


/* This code is adapted from the serial driver by Joris Borms
 * (jborms@users.sourceforge.net), and is available via Contiki projects.
 *
 * -- Fredrik Osterlind (fros@sics.se), 2011 */

/*
 * http://www.tinyos.net/tinyos-2.x/doc/html/tep113.html
 * http://mail.millennium.berkeley.edu/pipermail/tinyos-2-commits/2006-June/003534.html
 * http://www.capsule.hu/local/sensor/documentation/deciphering_tinyOS_serial_packets.pdf

 SF [ { Pr Seq Disp ( Dest Src len Grp Type | Payload ) } CRC ] EF

 SF      1   0x7E    Start Frame Byte
 Pr      1   0x45    Protocol Byte (SERIAL_PROTO_PACKET_NOACK)
 Seq     0           (not used) Sequence number byte, not used due to SERIAL_PROTO_PACKET_NOACK
 Disp    1   0x00    Packet format dispatch byte (TOS_SERIAL_ACTIVE_MESSAGE_ID)
 Dest    2   0xFFFF  (not used)
 Src     2   0x0000  (not used)
 len     1   N       Payload length
 Grp     1   0x00    Group
 Type    1           Message ID
 Payload N           The actual serial message
 CRC     2           Checksum of {Pr -> end of payload}
 EF      1   0x7E    End Frame Byte
 */

#include "deployment.h"
#include "dev/uart1.h"
#include "dev/serial-line.h"
#include "lib/ringbuf.h"

#if IN_INDRIYA
/* TODO handle multiple message types */

#ifndef AM_WILAB_CONTIKI_PRINTF
#define AM_WILAB_CONTIKI_PRINTF 65
#endif

#ifndef SERIAL_FRAME_SIZE_CONF
#define SERIAL_FRAME_SIZE 100
#else
#define SERIAL_FRAME_SIZE SERIAL_FRAME_SIZE_CONF
#endif

/* store all characters so we can calculate CRC */
static unsigned char serial_buf[SERIAL_FRAME_SIZE];
static unsigned char serial_buf_index = 0;

static u16_t crcByte(u16_t crc, u8_t b) {
  crc = (u8_t)(crc >> 8) | (crc << 8);
  crc ^= b;
  crc ^= (u8_t)(crc & 0xff) >> 4;
  crc ^= crc << 12;
  crc ^= (crc & 0xff) << 5;
  return crc;
}

int
putchar(int c)
{
  char ch = ((char) c);
  if (serial_buf_index < SERIAL_FRAME_SIZE){
    serial_buf[serial_buf_index] = ch;
    serial_buf_index++;
  }
  if (serial_buf_index == SERIAL_FRAME_SIZE || ch == '\n') {

    u8_t msgID = AM_WILAB_CONTIKI_PRINTF; // TODO look up type?

    /* calculate CRC */
    u16_t crc;
    crc = 0;
    crc = crcByte(crc, 0x45); // Pr byte
    crc = crcByte(crc, 0x00);
    crc = crcByte(crc, 0x0FF); crc = crcByte(crc, 0x0FF); // dest bytes
    crc = crcByte(crc, 0x00); crc = crcByte(crc, 0x00); // src bytes
    crc = crcByte(crc, SERIAL_FRAME_SIZE); // len byte
    crc = crcByte(crc, 0x00);
    crc = crcByte(crc, msgID);
    /* XXX since all of the above are constant, do we need to buffer
     * the characters to calculate CRC? Maybe we don't need the buffer? */
    int i;
    for (i=0; i<serial_buf_index; i++){
      crc = crcByte(crc, serial_buf[i]);
    }
    for (i=serial_buf_index; i<SERIAL_FRAME_SIZE; i++){
      crc = crcByte(crc, 0); // pad with zeroes
    }

    /* send message */
    uart1_writeb(0x7E);
    uart1_writeb(0x45);
    uart1_writeb(0x00);
    uart1_writeb(0x0FF); uart1_writeb(0x0FF);
    uart1_writeb(0x00); uart1_writeb(0x00);
    uart1_writeb(SERIAL_FRAME_SIZE);
    uart1_writeb(0x00);
    uart1_writeb(msgID);
    for (i=0; i<serial_buf_index; i++){
      /* test if bytes need to be escaped
      7d -> 7d 5d
      7e -> 7d 5e */
      if (serial_buf[i] == 0x7d){
        uart1_writeb(0x7d);
        uart1_writeb(0x5d);
      } else if (serial_buf[i] == 0x7e){
        uart1_writeb(0x7d);
        uart1_writeb(0x5e);
      } else {
        uart1_writeb(serial_buf[i]);
      }
    }
    for (i=serial_buf_index; i<SERIAL_FRAME_SIZE; i++){
      uart1_writeb(0); // pad with zeroes
    }
    // crc in reverse-byte oreder
    uart1_writeb((u8_t)(crc & 0x00FF));
    uart1_writeb((u8_t)((crc & 0xFF00) >> 8));
    uart1_writeb(0x7E);

    serial_buf_index = 0;
  }
  return c;
}

void
uart1_tinyos_frames(int active) {

}

#elif IN_TWIST

#ifndef UART1_PUTCHAR_CONF_BUF_SIZE
#define UART1_PUTCHAR_BUF_SIZE 100
#else
#define UART1_PUTCHAR_BUF_SIZE UART1_PUTCHAR_CONF_BUF_SIZE
#endif

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define SYNCH_BYTE 0x7e
#define ESCAPE_BYTE 0x7d

static int tinyos_active = 0;

/* TX */
static unsigned char serial_buf[UART1_PUTCHAR_BUF_SIZE];
static unsigned char serial_buf_index = 0;

static uint8_t msg_id = 65;

/* RX */
#define BUFSIZE 128 /* XXX TinyOS messages can up to be 256 bytes */
static struct ringbuf rxbuf;
static uint8_t rxbuf_data[BUFSIZE];

/*---------------------------------------------------------------------------*/
static u16_t
crc_byte(u16_t crc, u8_t b)
{
  crc = (u8_t)(crc >> 8) | (crc << 8);
  crc ^= b;
  crc ^= (u8_t)(crc & 0xff) >> 4;
  crc ^= crc << 12;
  crc ^= (crc & 0xff) << 5;
  return crc;
}
/*---------------------------------------------------------------------------*/
static uint16_t
writeb_crc(unsigned char c, uint16_t crc)
{
  /* Escape bytes:
        7d -> 7d 5d
        7e -> 7d 5e */
  if(c == ESCAPE_BYTE){
    uart1_writeb(ESCAPE_BYTE);
    uart1_writeb(0x5d);
  } else if(c == SYNCH_BYTE){
    uart1_writeb(ESCAPE_BYTE);
    uart1_writeb(0x5e);
  } else {
    uart1_writeb(c);
  }

  return crc_byte(crc, c);
}
/*---------------------------------------------------------------------------*/
int
putchar(int c)
{
  int i;
  uint16_t crc;
  char ch = ((char) c);

  if(!tinyos_active) {
    uart1_writeb(ch);
    return c;
  }

  /* Buffer outgoing character until newline, needed to determine the payload
   * length.
   */
  if(serial_buf_index < UART1_PUTCHAR_BUF_SIZE) {
    serial_buf[serial_buf_index++] = ch;
  }
  if(serial_buf_index < UART1_PUTCHAR_BUF_SIZE && ch != '\n') {
    return c;
  }

  /* Packetize and write buffered characters to serial port */

  /* Start of frame */
  crc = 0;
  uart1_writeb(0x7E);

  /* Protocol (noack) */
  crc = writeb_crc(0x45, crc);

  /* Sequence */
  crc = writeb_crc(0x00, crc);

  /* Destination */
  crc = writeb_crc(0xFF, crc);
  crc = writeb_crc(0xFF, crc);

  /* Source */
  crc = writeb_crc(0x00, crc);
  crc = writeb_crc(0x00, crc);

  /* Payload length */
  crc = writeb_crc(serial_buf_index, crc);

  /* Group */
  crc = writeb_crc(0x00, crc);

  /* Message ID */
  crc = writeb_crc(msg_id, crc);

  /* Payload characters */
  for (i=0; i < serial_buf_index; i++) {
    crc = writeb_crc(serial_buf[i], crc);
  }

  /* CRC */
  /* Note: calculating but ignoring CRC for these two... */
  writeb_crc((uint8_t) (crc & 0xFF), 0);
  writeb_crc((uint8_t) ((crc >> 8) & 0xFF), 0);

  /* End of frame */
  uart1_writeb(0x7E);
  serial_buf_index = 0;
  return c;
}
/*---------------------------------------------------------------------------*/
void
uart1_set_msgid(uint8_t id)
{
  msg_id = id;
}
/*---------------------------------------------------------------------------*/
static int
tinyos_serial_line_input_byte(unsigned char c)
{
  static int frame_pos = 0;
  static int error = 0;
  static int escaped = 0;
  static int payload_length = 0;
  static int crc_in = 0; /* CRC we calculate from incoming bytes */
  static int crc_footer = 0; /* CRC they put in packet footer */
  static int crc_pos = 0;
  static int full_packet = 0; /* CRC ok, let's send an ack soon */
  static unsigned char prefix = 0; /* seq */

  if(c == SYNCH_BYTE) {
    /* send ack? */
    if(full_packet) {
      uint16_t crc;

      uart1_writeb(SYNCH_BYTE);
      crc = 0;
      crc = writeb_crc(0x43, crc); /* SERIAL_PROTO_ACK */
      crc = writeb_crc(prefix, crc); /* prefix */

      writeb_crc((uint8_t)(crc & 0x00FF), 0);
      writeb_crc((uint8_t)((crc & 0xFF00) >> 8), 0);

      uart1_writeb(SYNCH_BYTE);
      full_packet = 0;
      PRINTF("uart1-putchar.c: ack\n");

      while (ringbuf_elements(&rxbuf) > 0) {
        int r = ringbuf_get(&rxbuf);
        PRINTF("uart1-putchar.c: serial line 0x%02x\n", r);
        serial_line_input_byte(r);
      }
    }

    /* Clear ring buffer */
    while (ringbuf_elements(&rxbuf) > 0) {
      ringbuf_get(&rxbuf);
    }

    error = 0;
    frame_pos = 0;
    escaped = 0;
    payload_length = -1; /* OBS: non-zero for crc_in below */
    crc_in = 0;
    crc_pos = 0;
    crc_footer = 0;
    return 0;
  }

  if (error) {
    return 0;
  }

  /* escaped characters */
  if(c == ESCAPE_BYTE) {
    escaped = 1;
    return 0;
  }
  if(escaped) {
    escaped = 0;
    if(c == 0x5d) {
      c = ESCAPE_BYTE;
    } else if(c == 0x5e) {
      c = SYNCH_BYTE;
    } else {
      PRINTF("uart1-putchar.c: error: unknown escaped byte 0x%x\n", c);
    }
  }

  frame_pos++;
  if(payload_length != 0) {
    crc_in = crc_byte(crc_in, c); /* From Start-of-Frame to last payload */
  }

  if(frame_pos == 1) {
    /* protocol byte */
    if (c != 0x44 /*SERIAL_PROTO_PACKET_ACK*/) {
      PRINTF("uart1-putchar.c: error: protocol byte is 0x%x\n", c);
      error = 1;
    }
    return 0;
  }
  if(frame_pos == 2) {
    /* prefix used in ack */
    prefix = c;
    return 0;
  }
  if(frame_pos == 8) {
    /* payload length first 8 bits */
    payload_length = c;
    return 0;
  }
  if(frame_pos < 11) {
    /* ignored */
    return 0;
  }
  if(payload_length > 0) {
    payload_length--;
    if (!ringbuf_put(&rxbuf, c)) {
      PRINTF("uart1-putchar.c: error: buffer full\n");
      error = 1;
    }
    return 0;
  }

  if(crc_pos == 0) {
    crc_footer = c&0xFF;
    crc_pos++;
  } else if(crc_pos == 1) {
    crc_footer += (c << 8);

    if(crc_footer == crc_in) {
      /* CRC ok: send ACK next SYNCH_BYTE */
      full_packet = 1;
    } else {
      /* CRC did not match */
      full_packet = 0;
    }
  }

  return 0;
}
/*---------------------------------------------------------------------------*/
void
uart1_tinyos_frames(int active)
{
  ringbuf_init(&rxbuf, rxbuf_data, sizeof(rxbuf_data));

  if(active) {
    uart1_set_input(tinyos_serial_line_input_byte);
  } else {
    /* XXX Assumes serial line input handler */
    uart1_set_input(serial_line_input_byte);
  }
  tinyos_active = active;
}
/*---------------------------------------------------------------------------*/
#else
/* Original Contiki-only version */
int
putchar(int c)
{
  uart1_writeb((char)c);
  return c;
}
#endif
