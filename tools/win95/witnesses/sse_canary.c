/* E01-S01 — source d'epreuve du verificateur de jeu d'instructions.
 *
 * Compilee uniquement quand `DKR_WIN95_SELFTEST_SSE` est actif, et alors avec
 * `-msse -mfpmath=sse`. Elle produit du SSE que le controle post-lien doit
 * refuser : c'est ainsi que l'on verifie le verificateur au lieu de le supposer
 * correct.
 *
 * Un verificateur casse ressemble exactement a un verificateur satisfait.
 *
 * En `float` et non en `double` : `-msse` seul ne couvre que la simple
 * precision, et GCC retomberait sur x87 pour les doubles — l'injection serait
 * alors inerte, ce qui a ete constate avant de corriger.
 */
float dkr_sse_canary(float a, float b)
{
    return a * b + a / b - a;
}
