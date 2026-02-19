# Channel Buffering Explained

## 1. Unbuffered Channels (Default)
`decl: [ch, chan<i32>]` or `call: [ch.init, 0]`

*   **Synchronous**: Sender and Receiver meet at the same time.
*   **Behavior**:
    *   `send(val)` BLOCKS until someone `recv()`s.
    *   `recv()` BLOCKS until someone `send(val)`s.
*   **Use case**: Synchronization point (handshake) between threads. guarantees that the data was received.

## 2. Buffered Channels
`call: [ch.init, 5]` (Buffer size 5)

*   **Asynchronous**: Decouples Sender and Receiver via a queue.
*   **Behavior**:
    *   `send(val)`: 
        *   If Buffer < 5: Adds to queue and continues immediately (does NOT block).
        *   If Buffer == 5 (Full): BLOCKS until space is freed (someone calls `recv`).
    *   `recv()`:
        *   If Buffer > 0: Takes from queue immediately (does NOT block).
        *   If Buffer == 0 (Empty): BLOCKS until data arrives.
*   **Use case**: Handling bursts of data, worker queues, decoupling fast producers from slow consumers.

## Example Scenario

### Unbuffered (Capacity 0)
- **Sender**: "Here is data." (Waits...)
- **Receiver**: (Arrives) "Got it."
- **Sender**: (Continues)

### Buffered (Capacity 10)
- **Sender**: "Here is data." (Drops it in box, 1/10 used, Continues immediately)
- **Sender**: "Here is more." (Drops it in box, 2/10 used, Continues immediately)
- ...
- **Receiver**: (Arrives later) "Oh loops, box has 2 items." (Takes item 1, leaving 1/10)
