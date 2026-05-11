// ===================================
// Count Errors
// ===================================
@initialize:python@
@@
global errors
errors = 0

// ===================================
// Ensure identifiers are used
// ===================================
@api_check@
expression E;
position p;
@@
(
    MEM_ALLOC(..., E)@p
|
    mem_alloc(..., E)@p
|
    MEM_REF(E)@p
|
    mem_ref(E)@p
|
    EVENT_ALLOC(..., E)@p
|
    event_alloc(..., E)@p
|
    EVENT_REF(E)@p
|
    event_ref(E)@p
|
    STOW_GET(..., E)@p
|
    stow_get(..., E)@p
|
    STOW_DECODE(..., E)@p
|
    stow_decode(..., E)@p
|
    MEM_UNREF(E)@p
|
    mem_unref(E)@p
|
    EVENT_UNREF(E)@p
|
    event_unref(E)@p
|
    STOW_RELEASE(..., E)@p
|
    stow_release(..., E)@p
|
    PASS_OWNERSHIP(E)@p
|
    NOT_REFERENCED(E)@p
)

@script:python@
e << api_check.E;
p << api_check.p;
@@
if "->" in e or "." in e or "[" in e or "(" in e:
    global errors
    errors += 1
    print(f"[{p[0].file}:{p[0].line}] POLICY ERROR: Used an expression ({e}) instead of an identifier. Static analysis requires the use of identifiers!")


// ===================================
// Check Memory management
// ===================================
@mem_leak exists@
identifier ptr;
type T;
position p1, p2;
@@

(
    MEM_ALLOC(..., (T)&ptr)@p1
|
    MEM_ALLOC(..., &ptr)@p1
|
    MEM_ALLOC(..., ptr)@p1
|
    mem_alloc(..., (T)&ptr)@p1
|
    mem_alloc(..., &ptr)@p1
|
    mem_alloc(..., ptr)@p1
|
    MEM_REF((T)&ptr)@p1
|
    MEM_REF(&ptr)@p1
|
    MEM_REF(ptr)@p1
|
    mem_ref((T)&ptr)@p1
|
    mem_ref(&ptr)@p1
|
    mem_ref(ptr)@p1
|
    EVENT_ALLOC(..., (T)&ptr)@p1
|
    EVENT_ALLOC(..., &ptr)@p1
|
    EVENT_ALLOC(..., ptr)@p1
|
    event_alloc(..., (T)&ptr)@p1
|
    event_alloc(..., &ptr)@p1
|
    event_alloc(..., ptr)@p1
|
    EVENT_REF((T)&ptr)@p1
|
    EVENT_REF(&ptr)@p1
|
    EVENT_REF(ptr)@p1
|
    event_ref((T)&ptr)@p1
|
    event_ref(&ptr)@p1
|
    event_ref(ptr)@p1
|
    STOW_GET(..., (T)&ptr)@p1
|
    STOW_GET(..., &ptr)@p1
|
    STOW_GET(..., ptr)@p1
|
    stow_get(..., (T)&ptr)@p1
|
    stow_get(..., &ptr)@p1
|
    stow_get(..., ptr)@p1
|
    STOW_DECODE(..., (T)&ptr)@p1
|
    STOW_DECODE(..., &ptr)@p1
|
    STOW_DECODE(..., ptr)@p1
|
    stow_decode(..., (T)&ptr)@p1
|
    stow_decode(..., &ptr)@p1
|
    stow_decode(..., ptr)@p1
)
...
when != MEM_UNREF((T)&ptr)
when != MEM_UNREF(&ptr)
when != MEM_UNREF(ptr)
when != mem_unref((T)&ptr)
when != mem_unref(&ptr)
when != mem_unref(ptr)
when != EVENT_UNREF((T)&ptr)
when != EVENT_UNREF(&ptr)
when != EVENT_UNREF(ptr)
when != event_unref((T)&ptr)
when != event_unref(&ptr)
when != event_unref(ptr)
when != STOW_RELEASE(..., (T)&ptr)
when != STOW_RELEASE(..., &ptr)
when != STOW_RELEASE(..., ptr)
when != stow_release(..., (T)&ptr)
when != stow_release(..., &ptr)
when != stow_release(..., ptr)
when != PASS_OWNERSHIP((T)&ptr)
when != PASS_OWNERSHIP(&ptr)
when != PASS_OWNERSHIP(ptr)
when != NOT_REFERENCED((T)&ptr)
when != NOT_REFERENCED(&ptr)
when != NOT_REFERENCED(ptr)
return ...;@p2

@script:python@
ptr << mem_leak.ptr;
p1 << mem_leak.p1;
p2 << mem_leak.p2;
@@

file_name = p1[0].file
alloc_line = p1[0].line
leak_line = p2[0].line

global errors
errors += 1
print(f"[{file_name}] ERROR: Variable '{ptr}' allocated/referenced on line {alloc_line} was not dereferenced. Leaked at line {leak_line}")

// ===================================
// Check irq_lock/irq_unlock
// ===================================
@irq_lock_leak exists@
identifier key;
position p1, p2;
@@
key = irq_lock()@p1
...
when != irq_unlock(key)
return ...;@p2

@script:python@
key << irq_lock_leak.key;
p1 << irq_lock_leak.p1;
p2 << irq_lock_leak.p2;
@@
file_name = p1[0].file
lock_line = p1[0].line
return_line = p2[0].line

global errors
errors += 1
print(f"[{file_name}] ERROR: Variable '{key}' locked on line {lock_line} was not unlocked. Leaked lock at line {return_line}")

// ===================================
// Fail if errors were detected
// ===================================
@finalize:python@
@@
import sys
if errors > 0:
    sys.exit(1)
else:
    print("SUCCESS: No resource leaks detected.")