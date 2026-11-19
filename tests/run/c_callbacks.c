/* C half of tests/run/c_callbacks.nc: calls NovaC functions exported with
 * extern "C", including through renamed symbols. */
int nova_add(int a, int b);
long nova_mix(signed char a, unsigned short b, double c);
extern const int nova_magic;

int c_drive(int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s = nova_add(s, i);
    if (nova_mix(-3, 65535, 2.5) != 65534) return -1;
    return s + nova_magic;
}
