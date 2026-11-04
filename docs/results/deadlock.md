# Deadlock lab demo

This report uses a standalone lab that models exam-server resources; it is not part of the live threaded simulation.

The four necessary conditions for deadlock are mutual exclusion, hold and wait, no preemption, and circular wait. Banker avoidance prevents unsafe states before grant. Detection followed by process abortion breaks circular wait after the fact.

## Banker's algorithm (safety and requests)

The safety algorithm applies the deterministic lowest-index rule to the textbook state and prints its safe sequence. The textbook request by P1 for (1 0 2) is granted, after which the written tentative state is used for two denial examples.

Command: ./deadsim safe --file scenarios/banker_textbook.txt
```text
state: safe
sequence: 1 3 0 2 4
```

Command: ./deadsim request --file scenarios/banker_textbook.txt --pid 1 --req "1 0 2"
```text
pid: 1
request: 1 0 2
result: granted
reason: ok
sequence: 1 3 0 2 4
```

Command: ./deadsim request --file /tmp/deadsim_demo.mQYCV5/post_grant.txt --pid 4 --req "3 3 0"
```text
pid: 4
request: 3 3 0
result: denied
reason: exceeds_available
sequence: none
```

Command: ./deadsim request --file /tmp/deadsim_demo.mQYCV5/post_grant.txt --pid 0 --req "0 2 0"
```text
pid: 0
request: 0 2 0
result: denied
reason: unsafe
sequence: none
```

Command: ./deadsim safe --file scenarios/exam_unsafe.txt
```text
state: unsafe
sequence: none
```

The textbook state has deterministic sequence 1 3 0 2 4; P1 is granted while P4 and P0 are denied for different reasons.
The exam state is unsafe. The P1 grant is written to a temporary state file and removed when the demo exits.

## Deadlock detection

Detection starts with currently available resources and repeatedly completes the lowest-index eligible session. Processes holding no resources begin finished.

Command: ./deadsim detect --file scenarios/detect_textbook_ok.txt
```text
deadlock: no
finish_order: 0 2 1 3 4
deadlocked: none
```
Command: ./deadsim detect --file scenarios/detect_textbook_deadlock.txt
```text
deadlock: yes
finish_order: 0
deadlocked: 1 2 3 4
```
Command: ./deadsim detect --file scenarios/exam_deadlock.txt
```text
deadlock: yes
finish_order: none
deadlocked: 1 2 3 4
```
The textbook examples demonstrate both outcomes, while the exam case leaves multiple sessions unfinished.
Processes with zero allocation begin finished and are omitted from finish_order.

## Recovery (victim selection policies)

Recovery aborts one currently deadlocked session at a time and releases its allocation. Victim selection changes the abort order; each policy is demonstrated on the textbook deadlock state.

Command: ./deadsim recover --file scenarios/detect_textbook_deadlock.txt
```text
deadlock: yes
aborted: 1
final: no_deadlock
```
Command: ./deadsim recover --file scenarios/detect_textbook_deadlock.txt --policy max-alloc
```text
deadlock: yes
aborted: 2
final: no_deadlock
```
Command: ./deadsim recover --file scenarios/detect_textbook_deadlock.txt --policy lowest-index
```text
deadlock: yes
aborted: 1
final: no_deadlock
```
Command: ./deadsim recover --file scenarios/detect_textbook_deadlock.txt --policy highest-index
```text
deadlock: yes
aborted: 4
final: no_deadlock
```
The selected victim is aborted and its allocation is returned before detection runs again.
The initial deadlock result stays visible even when recovery reaches a no-deadlock final state.

## Wait-for graph

A wait-for graph reports every node that can reach itself through one or more edges. A chain has no cycle; the directed triangle has a cycle.

Command: ./deadsim wfg --nodes 3 --edges "0>1 1>2 2>0"
```text
deadlock: yes
on_cycle: 0 1 2
```

Command: ./deadsim wfg --nodes 3 --edges "0>1 1>2"
```text
deadlock: no
on_cycle: none
```

A node is listed only if a nonempty directed path returns to that same node.
The triangle lists all three nodes; the chain lists none.
