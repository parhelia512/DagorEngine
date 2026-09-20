options gen2
require dastest/testing_boost
require daslib/math_boost

let inf = 1.0 / 0.0         // nolint:LINT006 — deliberate: this test is about IEEE inf/nan
let neg_inf = -1.0 / 0.0    // nolint:LINT006
let neg_nan = inf / inf     // nolint:LINT007 — inf/inf is how a nan is produced here
let nan = -(inf / inf)      // nolint:LINT007

// Hides the value from constant folding: the compiler cannot see through the call.
[sideeffects]
def private opaque(x) {
    return x
}

// A host may build the runtime with -ffinite-math-only (dagor does). That folds every nan
// comparison to a constant, so the compare semantics below are not the ones that host
// asked for - detect it and skip. is_nan / is_finite are exempt: they answer IEEE on every
// host (test_is_nan_and_is_finite_hold_on_every_host), so the probe is a compare, never is_nan.
def private host_folds_nan_compares() : bool {
    let a = opaque(unsafe(reinterpret<double>(0x7FF8000000000000ul)))
    let b = opaque(unsafe(reinterpret<double>(0x7FF8000000000000ul)))
    return a == b || !(a != b)
}

[sideeffects]
def eq(a, b) {
    return a == b
}

[sideeffects]
def neq(a, b) {
    return a != b
}

[sideeffects]
def less(a, b) {
    return a < b
}

[sideeffects]
def greater(a, b) {
    return a > b
}

[sideeffects]
def leq(a, b) {
    return a <= b
}

[sideeffects]
def geq(a, b) {
    return a >= b
}

[test]
def test_inf_and_nan(t : T?) {
    if (host_folds_nan_compares()) {
        t |> skip("host runtime is built -ffinite-math-only: nan compares fold")
        return
    }
    t |> run("inf and neg inf") @@(t : T?) {
        t |> strictEqual(100.0 / 0.0, inf)
        t |> strictEqual(-100.0 / 0.0, neg_inf)
        t |> strictEqual(inf / inf, neg_nan)
        t |> strictEqual(-(inf / inf), nan)
    }

    t |> run("is_finite and is_nan") @@(t : T?) {
        t |> success(!is_finite(inf))
        t |> success(!is_finite(neg_inf))
        t |> success(!is_finite(nan))
        t |> success(!is_finite(neg_nan))
        t |> success(is_finite(1.))
        t |> success(is_finite(0.))
        t |> success(is_nan(nan))
        t |> success(is_nan(neg_nan))
        t |> success(!is_nan(inf))
        t |> success(!is_nan(neg_inf))
        t |> success(!is_nan(1.))
        t |> success(!is_nan(0.))
    }

    t |> run("inf and nan comparisons") @@(t : T?) {
        t |> success(!eq(nan, nan))
        t |> success(!eq(1., nan))
        t |> success(!eq(nan, 1.))
        t |> success(!eq(inf, nan))
        t |> success(!eq(nan, inf))

        t |> success(neq(nan, nan))
        t |> success(neq(1., nan))
        t |> success(neq(nan, 1.))
        t |> success(neq(inf, nan))
        t |> success(neq(nan, inf))

        t |> success(!less(nan, nan))
        t |> success(!less(1., nan))
        t |> success(!less(nan, 1.))
        t |> success(!less(inf, nan))
        t |> success(!less(nan, inf))

        t |> success(!leq(nan, nan))
        t |> success(!leq(1., nan))
        t |> success(!leq(nan, 1.))
        t |> success(!leq(inf, nan))
        t |> success(!leq(nan, inf))

        t |> success(!greater(nan, nan))
        t |> success(!greater(1., nan))
        t |> success(!greater(nan, 1.))
        t |> success(!greater(inf, nan))
        t |> success(!greater(nan, inf))

        t |> success(!geq(nan, nan))
        t |> success(!geq(1., nan))
        t |> success(!geq(nan, 1.))
        t |> success(!geq(inf, nan))
        t |> success(!geq(nan, inf))
    }
}

// bit-built values, so no arithmetic a finite-math host could fold; this test never skips
[test]
def test_is_nan_and_is_finite_hold_on_every_host(t : T?) {
    t |> run("float") @@(tf : T?) {
        let fnan = opaque(unsafe(reinterpret<float>(0x7FC00000u)))
        let fneg_nan = opaque(unsafe(reinterpret<float>(0xFFC00000u)))
        let finf = opaque(unsafe(reinterpret<float>(0x7F800000u)))
        let fneg_inf = opaque(unsafe(reinterpret<float>(0xFF800000u)))
        let fmax = opaque(unsafe(reinterpret<float>(0x7F7FFFFFu)))
        let fone = opaque(1.0f)
        tf |> success(is_nan(fnan))
        tf |> success(is_nan(fneg_nan))
        tf |> success(!is_nan(finf))
        tf |> success(!is_nan(fneg_inf))
        tf |> success(!is_nan(fmax))
        tf |> success(!is_nan(fone))
        tf |> success(!is_finite(fnan))
        tf |> success(!is_finite(fneg_nan))
        tf |> success(!is_finite(finf))
        tf |> success(!is_finite(fneg_inf))
        tf |> success(is_finite(fmax))
        tf |> success(is_finite(fone))
    }
    t |> run("double") @@(td : T?) {
        let dnan = opaque(unsafe(reinterpret<double>(0x7FF8000000000000ul)))
        let dneg_nan = opaque(unsafe(reinterpret<double>(0xFFF8000000000000ul)))
        let dinf = opaque(unsafe(reinterpret<double>(0x7FF0000000000000ul)))
        let dneg_inf = opaque(unsafe(reinterpret<double>(0xFFF0000000000000ul)))
        let dmax = opaque(unsafe(reinterpret<double>(0x7FEFFFFFFFFFFFFFul)))
        let done = opaque(1.0lf)
        td |> success(is_nan(dnan))
        td |> success(is_nan(dneg_nan))
        td |> success(!is_nan(dinf))
        td |> success(!is_nan(dneg_inf))
        td |> success(!is_nan(dmax))
        td |> success(!is_nan(done))
        td |> success(!is_finite(dnan))
        td |> success(!is_finite(dneg_nan))
        td |> success(!is_finite(dinf))
        td |> success(!is_finite(dneg_inf))
        td |> success(is_finite(dmax))
        td |> success(is_finite(done))
    }
}


