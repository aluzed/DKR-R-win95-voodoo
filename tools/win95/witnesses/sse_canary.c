/* E01-S01 - test source for the instruction-set checker.
 *
 * Compiled only when `DKR_WIN95_SELFTEST_SSE` is active, and then with
 * `-msse -mfpmath=sse`. It produces SSE that the post-link check must refuse:
 * that is how the checker is verified instead of being assumed correct.
 *
 * A broken checker looks exactly like a satisfied one.
 *
 * In `float` and not in `double`: `-msse` alone only covers single precision,
 * and GCC would fall back to x87 for doubles - the injection would then be
 * inert, which was observed before it was fixed.
 */
float dkr_sse_canary(float a, float b)
{
    return a * b + a / b - a;
}
