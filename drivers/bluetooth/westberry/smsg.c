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
} smsg_t;

static smsg_message_t smsg_buffer[SMSG_NUM] __attribute__((aligned(PORT_NATURAL_ALIGN)));
static msg_t smsg_mailbox_buffer[SMSG_NUM];
static smsg_t smsg_instance;

void smsg_init(void) {
    chFifoObjectInit(&smsg_instance.fifo, sizeof(smsg_message_t), SMSG_NUM, smsg_buffer, smsg_mailbox_buffer);
    smsg_instance.state = smsg_state_free;
}

smsg_message_t *smsg_take(void) {
    // Acquire a message to the pool
    return (smsg_message_t *)chFifoTakeObjectTimeout(&smsg_instance.fifo, TIME_IMMEDIATE);
}


void smsg_return(smsg_message_t *msg) {
    // Return the current message to the pool
    if (msg != NULL) {
        chFifoReturnObject(&smsg_instance.fifo, msg);
    }
}

void smsg_send(smsg_message_t *msg) {
    chFifoSendObject(&smsg_instance.fifo, msg);
}

smsg_message_t *smsg_receive(void) {
    smsg_message_t *msg = NULL;
    msg_t result = chFifoReceiveObjectTimeout(&smsg_instance.fifo, (void **)&msg, TIME_IMMEDIATE);
    if (result != MSG_OK) {
        return NULL;
    }
    return msg;
}

smsg_states_t smsg_get_state(void) {
    return smsg_instance.state;
}

void smsg_set_state(smsg_states_t state) {
    smsg_instance.state = state;
}

bool smsg_is_busy(void) {
    // Check if there are messages in the mailbox
    return (chMBGetUsedCountI(&smsg_instance.fifo.mbx) > 0);
}
