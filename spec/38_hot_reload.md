# Hot Reload & Function Versioning

## 1. Concept: Runtime Updates
Hot reloading allows updating function implementations without restarting the application or invalidating existing references.

## 2. Versioning
Every function definition carries a `version` field (u64, default 0). The compiler tracks this version to manage updates.

## 3. Update Mechanism (Memory Layout)
To support seamless updates, we employ a strategy of **overwriting** and **relocation**.

### 3.1 Pre-Allocation (Padding)
When a function is first compiled and allocated in memory:
*   We allocate **more memory than required** for the function body (e.g., `actual_size + padding`).
*   This extra space anticipates future growth of the function code during updates.

### 3.2 Updating a Function (The Swap)
When a new version of a function is loaded:

1.  **Duplicate Old Code (Backup)**:
    *   The *current* (old) machine code is copied from its original location to a *new* memory block.
    *   This ensures that any executing threads can continue running the old version safely if we redirect their instruction pointers or if they are executing position-independent code.
    
2.  **Overwrite In-Place**:
    *   The **original memory location** (the function's entry point) is **directly overwritten** with the code of the *new* version.
    *   Because existing calls point to this address, they will immediately start executing the new version on the next call.
    *   If the new function body fits within the pre-allocated space (original size + padding), no trampoline is needed.

3.  **Handling Overflow**:
    *   If the new version exceeds the pre-allocated space, a **trampoline (JMP)** is written at the original address, redirecting execution to a larger memory block where the full new function resides.

## 4. Memory Reclamation (Garbage Collection)
Old versions of functions (the "backups" created in step 3.1) occupy memory.

*   **Operator `sys.free_version`**: 
    *   We provide a system operator to explicitly free the memory of these old versions.
    *   **Usage:** `call: [sys.free_version, function_name, version_id]`
    *   **Safety:** This must only be called when it is guaranteed that no thread is executing the old code anymore (e.g., after a grace period or quiescence).

## 5. Safety Considerations
*   **Thread Safety**: The overwrite operation must be atomic or performed during a safe-point (e.g., when no threads are executing the function prologue).
*   **Instruction Pointers**: The "relocate old code" step ensures that if we can patch the return addresses or if the code is position-independent, old executions finish safely.