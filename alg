C = candidate group
S = set of accepted groups
T = target group flops: flops of 1st accepted group
M = max flops of first group
P.h = max hosts per group
P.g = max # groups
P.w = max variation within a group (ratio 1st to ith)
P.g = max variation between groups (ratio either way)
P.a = bool: (aggressive) use a host even if it would put this group
    above the threshold
    (make this job fast, possibly at expense of other jobs)

for each host H
    if not enough external storage: skip

    if S is empty
        // no accepted groups so far.
        // try to add H to the candidate group
        // this is the first group, so we need to enforce M

        // loop because of inner storage pruning
        while 1
            if C.flops + H.flops > M
                // H would make C too fast; see if we can accept C
                if prune C for inner storage
                    continue
                add C to S
                C = {H}
                goto next_host
            else
                add H to C
                if C.size == P.h
                    if prune C for inner storage
                        remove H from C
                        continue
                    // C is full.  Make it the first group
                    add C to S
                    C = empty
                    goto next_host
                else
                    // C is not full.  maybe we can add more hosts
                    goto next_host
    else
        // there is at least 1 accepted group, so we have to enforce P.g
        // C.flops is < T, otherwise we would have already accepted it
        while 1
            x = C.flops + H.flops
            if x > M or (!p.a and x > T*P.g)
                // H would make C too fast; see if we can accept C as is
                if prune C for inner storage
                    continue
                if C.flops < T/P.g
                    // C is too slow but adding H would make it too fast.
                    // Remove the 1st host from C
                    remove C[0]
                    continue
                add C to S
                C = {H}
                goto next_host
            else
                // adding H to C would not make it too fast
                add H to C
                if prune C for inner storage
                    goto next_host
                if C.size == P.h
                    // C is max size.
                    // see if it's fast enough
                    if C.flops > T/P.g
                        // yes; accept C
                        add C to S
                        goto next_host
                    else
                        // C is max size and still too slow.
                        // we can't form more acceptable groups
                        goto done
    next_host:

done: return
