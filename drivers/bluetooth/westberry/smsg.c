// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "smsg.h"
#include <string.h>
#include <ch.h>

#ifndef SMSG_NUM
#    define SMSG_NUM 40
#endif

typedef struct {
    smsg_states_t state;
    objects_fifo_t fifo;
    smsg_message_t *current_msg;  // Store the current peeked message
} smsg_t;

static smsg_message_t smsg_buffer[SMSG_NUM] __attribute__((aligned(PORT_NATURAL_ALIGN)));
static msg_t smsg_mailbox_buffer[SMSG_NUM];
static smsg_t smsg_instance;

void smsg_init(void) {
    chFifoObjectInit(&smsg_instance.fifo, sizeof(smsg_message_t), SMSG_NUM, smsg_buffer, smsg_mailbox_buffer);
    smsg_instance.state = smsg_state_free;
    smsg_instance.current_msg = NULL;
}

bool smsg_push(uint8_t *buf, uint32_t size) {
    if (size > SMSG_PAYLOAD_LEN) {
        return false;
    }

    smsg_message_t *msg = (smsg_message_t *)chFifoTakeObjectTimeout(&smsg_instance.fifo, TIME_IMMEDIATE);
    if (msg == NULL) {
        return false;
    }

    memcpy(msg->data, buf, size);
    msg->size = size;

    chFifoSendObject(&smsg_instance.fifo, msg);
    return true;
}

uint32_t smsg_peek(uint8_t *buf) {
    // If we don't have a current message, try to receive one
    if (smsg_instance.current_msg == NULL) {
        msg_t result = chFifoReceiveObjectTimeout(&smsg_instance.fifo, (void **)&smsg_instance.current_msg, TIME_IMMEDIATE);
        if (result != MSG_OK || smsg_instance.current_msg == NULL) {
            return 0;
        }
    }

    // Copy the data from the current message
    memcpy(buf, smsg_instance.current_msg->data, smsg_instance.current_msg->size);
    return smsg_instance.current_msg->size;
}

void smsg_pop(void) {
    // Return the current message to the pool and clear it
    if (smsg_instance.current_msg != NULL) {
        chFifoReturnObject(&smsg_instance.fifo, smsg_instance.current_msg);
        smsg_instance.current_msg = NULL;
    }
}

smsg_states_t smsg_get_state(void) {
    return smsg_instance.state;
}

void smsg_set_state(smsg_states_t state) {
    smsg_instance.state = state;
}

bool smsg_is_busy(void) {
    // Check if we have a current message or if there are messages in the mailbox
    return (smsg_instance.current_msg != NULL) || (chMBGetUsedCountI(&smsg_instance.fifo.mbx) > 0);
}
