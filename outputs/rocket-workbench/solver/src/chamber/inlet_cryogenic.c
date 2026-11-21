/* 文件功能：调用 CoolProp 状态方程计算液氧、液氢和液甲烷入口物性。
 * 输入：液体组元、入口温度 K 和绝对压力 Pa。 
 * 输出：入口摩尔焓、热容及适用性标志。 */
#include "inlet_cryogenic.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>

typedef double (__cdecl *PropsFunction)(const char *, const char *, double,
                                      const char *, double, const char *);
typedef double (__cdecl *ConstantFunction)(const char *, const char *);
typedef long (__cdecl *StringFunction)(const char *, char *, int);
static PropsFunction props;
static ConstantFunction constant;
static StringFunction global_string;
static HMODULE library;

/* 功能：加载指定的 CoolProp 动态库及所需函数。
 * 输入：error、size。
 * 输出：成功返回 1；失败写入错误信息。 */
static int load_library(char *error, size_t size)
{
    wchar_t path[32768], *end;
    FARPROC fn;
    char version[64] = {0};
    DWORD length;
    if (library) return 1;
    length = GetModuleFileNameW(NULL, path, 32768);
    if (!length || length >= 32700) goto failed;
    end = wcsrchr(path, L'\\');
    if (!end) goto failed;
    wcscpy(end+1, L"vendor\\coolprop\\CoolProp.dll");
    library = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) goto failed;

    _Static_assert(sizeof(fn) == sizeof(props), "Function pointer size");
    fn = GetProcAddress(library, "PropsSI"); memcpy(&props, &fn, sizeof(props));
    fn = GetProcAddress(library, "Props1SI"); memcpy(&constant, &fn, sizeof(constant));
    fn = GetProcAddress(library, "get_global_param_string"); memcpy(&global_string, &fn, sizeof(global_string));
    if (!props || !constant || !global_string) goto failed;
    global_string("version", version, (int)sizeof(version));
    if (strcmp(version, "8.0.0")) goto failed;
    return 1;
failed:
    if (library) FreeLibrary(library);
    library = NULL; props = NULL; constant = NULL; global_string = NULL;
    if (error && size) snprintf(error, size,
        "Cannot load CoolProp 8.0.0 x64: keep vendor/coolprop/CoolProp.dll beside the executable's directory.");
    return 0;
}
#endif

/* 功能：把低温物性错误写入缓冲区。
 * 输入：error、size、message。
 * 输出：返回失败值 0。 */
static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

/* 功能：按熔化线模型求给定压强的熔点。
 * 输入：index、p。
 * 输出：返回熔点 K。 */
static double melting_temperature(int index, double p)
{
    double a, b, c, T0;
    if (index == 0) { a=227606.348; b=-266999247.652; c=1.769; T0=1; }
    else if (index == 1) {
        a=125746.643; b=-21155737.752; c=1.955; T0=1;
        if (p > b+a*(pow(22.0,c)-1)) {
            a=248578.596; b=-26280332.904; c=1.764739;
        }
    } else { a=208000000; b=11700; c=1.698; T0=90.6941; }
    return T0*pow(1+(p-b)/a, 1/c);
}

/* 功能：用状态方程与液相边界检查求低温液体入口物性。
 * 输入：c、out、error、size。
 * 输出：成功返回 1 并写入 InletEnthalpyProperties。 */
int inlet_cryogenic_properties(const InletComponent *c, InletEnthalpyProperties *out,
                              char *error, size_t size)
{
#ifdef _WIN32
    static const char *fluids[] = {"Oxygen", "ParaHydrogen", "Methane"};
    static const double masses[] = {0.0319988, 0.00201588, 0.01604246};
    static const double Trefs[] = {90.170, 20.270, 111.643};
    static const double Hrefs[] = {-12979, -9012, -89233};
    static const double pmaxs[] = {80e6, 100e6, 100e6};
    int index;
    const char *fluid;
    double low, high, psat, H, Href, Cp, Tm;
    InletEnthalpyProperties r = {0,0,0};
    if (!c || !out || c->model < INLET_O2_LIQUID || c->model > INLET_CH4_LIQUID)
        return fail(error, size, "Invalid cryogenic liquid model.");
    index = (int)c->model - (int)INLET_O2_LIQUID;
    fluid = fluids[index];
    if (!isfinite(c->T) || !isfinite(c->p) || c->p <= 0 || c->p > pmaxs[index])
        return fail(error, size, "Invalid inlet T or p: use absolute Pa; p <= 80 MPa (O2) / 100 MPa (H2, CH4).");
    if (!isfinite(c->molar_mass) || fabs(c->molar_mass/masses[index]-1) > 1e-10)
        return fail(error, size, "Cryogenic molar mass differs from its NASA reference (kg/mol).");
    if (!load_library(error, size)) return 0;
    low = constant(fluid, "Ttriple");
    high = constant(fluid, "Tcrit");
    Tm = melting_temperature(index, c->p);
    if (!(c->T > fmax(low, Tm) && c->T < high)) {
        if (error && size) snprintf(error, size,
            "%s: at p=%.10g Pa require %.10g < T < %.10g K (melting/critical limits).",
            fluid, c->p, fmax(low,Tm), high);
        return 0;
    }
    psat = props("P", "T", c->T, "Q", 0, fluid);
    if (!isfinite(psat) || psat <= 0 || psat > 1e100)
        return fail(error, size, "EOS saturation-pressure calculation failed.");
    if (c->p < psat*(1-1e-6)) {
        if (error && size) snprintf(error, size,
            "%s is not liquid: p=%.10g Pa < saturation pressure %.10g Pa at T=%.10g K.",
            fluid, c->p, psat, c->T);
        return 0;
    }
    if (fabs(c->p/psat-1) <= 1e-6) {

        H = props("Hmass", "T", c->T, "Q", 0, fluid);
        Cp = props("Cpmass", "T", c->T, "Q", 0, fluid);
        r.warnings |= INLET_INFO_SATURATED_LIQUID;
    } else {
        H = props("Hmass", "T", c->T, "P", c->p, fluid);
        Cp = props("Cpmass", "T", c->T, "P", c->p, fluid);
    }

    Href = props("Hmass", "T", Trefs[index], "P", INLET_P0, fluid);
    if (!isfinite(H) || !isfinite(Href) || !isfinite(Cp) ||
        fabs(H)>1e100 || fabs(Href)>1e100 || Cp<=0 || Cp>1e100) {
        char detail[256] = {0};
        global_string("errstring", detail, (int)sizeof(detail));
        if (error && size) snprintf(error, size, "CoolProp calculation failed: %s", detail);
        return 0;
    }
    r.H_m = Hrefs[index]+masses[index]*(H-Href);
    r.Cp_m = masses[index]*Cp;
    if (index == 1) r.warnings |= INLET_INFO_PARAHYDROGEN;
    *out = r;
    if (error && size) error[0] = '\0';
    return 1;
#else
    (void)c; (void)out;
    return fail(error, size, "This build uses the Windows x64 CoolProp library.");
#endif
}
