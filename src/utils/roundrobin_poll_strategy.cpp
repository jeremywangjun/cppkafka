/*
 * Copyright (c) 2017, Matias Fontanini
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
 
#include "utils/roundrobin_poll_strategy.h"

using std::string;
using std::chrono::milliseconds;
using std::make_move_iterator;

namespace cppkafka {

RoundRobinPollStrategy::RoundRobinPollStrategy(Consumer& consumer)
: PollStrategyBase(consumer) {
    reset_state();
}

RoundRobinPollStrategy::~RoundRobinPollStrategy() {
    restore_forwarding();
}


Message RoundRobinPollStrategy::poll() {
    return poll(get_consumer().get_timeout());
}

Message RoundRobinPollStrategy::poll(milliseconds timeout) {
    // Always give priority to group and global events
    Message message = get_consumer_queue().queue.consume(milliseconds(0));
    if (message) {
        return message;
    }
    size_t num_queues = get_partition_queues().size();
    while (num_queues--) {
        //consume the next partition (non-blocking)
        message = get_next_queue().queue.consume(milliseconds(0));
        if (message) {
            return message;
        }
    }
    // We still don't have a valid message so we block on the event queue
    return get_consumer_queue().queue.consume(timeout);
}

MessageList RoundRobinPollStrategy::poll_batch(size_t max_batch_size) {
    return poll_batch(max_batch_size, get_consumer().get_timeout());
}

MessageList RoundRobinPollStrategy::poll_batch(size_t max_batch_size, milliseconds timeout) {
    MessageList messages;
    ssize_t count = max_batch_size;
    
    // batch from the group event queue first (non-blocking)
    consume_batch(get_consumer_queue().queue, messages, count, milliseconds(0));
    size_t num_queues = get_partition_queues().size();
    while ((count > 0) && (num_queues--)) {
        // batch from the next partition (non-blocking)
        consume_batch(get_next_queue().queue, messages, count, milliseconds(0));
    }
    // we still have space left in the buffer
    if (count > 0) {
        // wait on the event queue until timeout
        consume_batch(get_consumer_queue().queue, messages, count, timeout);
    }
    return messages;
}

void RoundRobinPollStrategy::consume_batch(Queue& queue,
                                           MessageList& messages,
                                           ssize_t& count,
                                           milliseconds timeout) {
    MessageList queue_messages = queue.consume_batch(count, timeout);
    if (queue_messages.empty()) {
        return;
    }
    // concatenate both lists
    messages.insert(messages.end(),
                    make_move_iterator(queue_messages.begin()),
                    make_move_iterator(queue_messages.end()));
    // reduce total batch count
    count -= queue_messages.size();
}


void RoundRobinPollStrategy::restore_forwarding() {
    // forward all partition queues
    for (const auto& toppar : get_partition_queues()) {
        toppar.second.queue.forward_to_queue(get_consumer_queue().queue);
    }
}

QueueData& RoundRobinPollStrategy::get_next_queue() {
    if (get_partition_queues().empty()) {
        throw QueueException(RD_KAFKA_RESP_ERR__STATE);
    }
    if (++queue_iter_ == get_partition_queues().end()) {
        queue_iter_ = get_partition_queues().begin();
    }
    return queue_iter_->second;
}

void RoundRobinPollStrategy::reset_state() {
    queue_iter_ = get_partition_queues().begin();
}

} //cppkafka
