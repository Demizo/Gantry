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
    MEM_REF(E)@p
|
    EVENT_ALLOC(..., E)@p
|
    EVENT_REF(E)@p
|
    datastore_get(..., E)@p
|
    datastore_decode(..., E)@p
|
    MEM_UNREF(E)@p
|
    EVENT_UNREF(E)@p
|
    datastore_release(..., E)@p
|
    PASS_OWNERSHIP(E)@p
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
    MEM_REF((T)&ptr)@p1
|
    MEM_REF(&ptr)@p1
|
    MEM_REF(ptr)@p1
|
    EVENT_ALLOC(..., (T)&ptr)@p1
|
    EVENT_ALLOC(..., &ptr)@p1
|
    EVENT_ALLOC(..., ptr)@p1
|
    EVENT_REF((T)&ptr)@p1
|
    EVENT_REF(&ptr)@p1
|
    EVENT_REF(ptr)@p1
|
    datastore_get(..., (T)&ptr)@p1
|
    datastore_get(..., &ptr)@p1
|
    datastore_get(..., ptr)@p1
|
    datastore_decode(..., (T)&ptr)@p1
|
    datastore_decode(..., &ptr)@p1
|
    datastore_decode(..., ptr)@p1
)
...
when != MEM_UNREF((T)&ptr)
when != MEM_UNREF(&ptr)
when != MEM_UNREF(ptr)
when != EVENT_UNREF((T)&ptr)
when != EVENT_UNREF(&ptr)
when != EVENT_UNREF(ptr)
when != datastore_release(..., (T)&ptr)
when != datastore_release(..., &ptr)
when != datastore_release(..., ptr)
when != PASS_OWNERSHIP((T)&ptr)
when != PASS_OWNERSHIP(&ptr)
when != PASS_OWNERSHIP(ptr)
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
// Fail if errors were detected
// ===================================
@finalize:python@
@@
import sys
if errors > 0:
    sys.exit(1)
else:
    print("SUCCESS: No memory leaks detected.")