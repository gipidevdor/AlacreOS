; Check that counterexamples to the IPC and EDF arithmetic contracts are unsatisfiable.
; Ring state follows Endpoints::invoke in kernel/ipc.cpp. Indices are mathematical
; integers here because head + count cannot exceed 7 for a four-slot ring.
(set-logic QF_LIA)
(declare-const head Int)
(declare-const count Int)
(declare-const offset Int)
(assert (and (<= 0 head) (< head 4) (<= 0 count) (<= count 4)))
(push 1)
; A successful send writes an unused slot and stays within the queue quota.
(assert (< count 4))
(assert (or (> (+ count 1) 4)
            (and (<= 0 offset) (< offset count)
                 (= (mod (+ head count) 4) (mod (+ head offset) 4)))))
(check-sat)
(pop 1)
(push 1)
; Receiving discards the first entry and preserves every remaining entry's index.
(assert (and (> count 0) (<= 1 offset) (< offset count)))
(assert (not (= (mod (+ (mod (+ head 1) 4) (- offset 1)) 4)
                (mod (+ head offset) 4))))
(check-sat)
(pop 1)
(reset)

; The low eight bits encode the local slot. The guard in Space::vacancy
; excludes wraparound of the 56-bit generation. Any previously issued
; generation for the same slot remains distinct after incrementing.
(set-logic QF_BV)
(declare-const slot (_ BitVec 8))
(declare-const previous (_ BitVec 56))
(declare-const current (_ BitVec 56))
(assert (bvule previous current))
(assert (not (= current (_ bv72057594037927935 56))))
(assert (= (concat previous slot) (concat (bvadd current (_ bv1 56)) slot)))
(check-sat)
(reset)

; boundary_of adds the budget only if it is less than the time to deadline.
(set-logic QF_BV)
(declare-const now (_ BitVec 64))
(declare-const deadline (_ BitVec 64))
(declare-const remaining (_ BitVec 64))
(assert (bvult now deadline))
(assert (bvult (_ bv0 64) remaining))
(assert (bvult remaining (bvsub deadline now)))
(define-fun boundary () (_ BitVec 64) (bvadd now remaining))
(assert (or (bvule boundary now) (bvuge boundary deadline)))
(check-sat)
