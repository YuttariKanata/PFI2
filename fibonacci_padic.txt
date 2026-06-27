#include <gmpxx.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;

namespace {

// 目的:
//   3^fib(n) mod 10^N を、直接 mod 10^N で冪乗せずに
//   mod 2^N と mod 5^N に分けて計算し、最後に CRT で戻す。
//
// p-adic 側の基本形:
//   mod 2^N では 3^e = 3^r * 9^q  = 3^r * exp(q log 9)
//   mod 5^N では 3^e = 3^r * 81^q = 3^r * exp(q log 81)
//
// 9=1+8, 81=1+80 なので log/exp の収束半径に入っている。
// 巨大指数を mpz_powm に渡さず、log/exp を Newton 倍精度で解くのが高速化の主眼。

#ifndef PADIC_LOG_REDUCTION_MIN
#define PADIC_LOG_REDUCTION_MIN 16UL
#endif

#ifndef PADIC_LOG_REDUCTION_MAX
#define PADIC_LOG_REDUCTION_MAX 4096UL
#endif

#ifndef PADIC_LOG_REDUCTION_NUM
#define PADIC_LOG_REDUCTION_NUM 7UL
#endif

#ifndef PADIC_LOG_REDUCTION_DEN
#define PADIC_LOG_REDUCTION_DEN 8UL
#endif

#ifndef PADIC_PARALLEL
#define PADIC_PARALLEL 1
#endif

pair<mpz_class, mpz_class> fib_mod_iter(uint64_t n, const mpz_class& mod) {
    if (mod == 1) return {0, 0};

    mpz_class a = 0;
    mpz_class b = 1;
    mpz_class c, d, two_b_minus_a;

    if (n == 0) return {a, b};

    int highest = 63 - __builtin_clzll(n);
    for (int bit = highest; bit >= 0; --bit) {
        two_b_minus_a = 2 * b - a;
        mpz_mod(two_b_minus_a.get_mpz_t(), two_b_minus_a.get_mpz_t(), mod.get_mpz_t());

        c = a * two_b_minus_a;
        mpz_mod(c.get_mpz_t(), c.get_mpz_t(), mod.get_mpz_t());

        d = a * a + b * b;
        mpz_mod(d.get_mpz_t(), d.get_mpz_t(), mod.get_mpz_t());

        if ((n >> bit) & 1ULL) {
            a.swap(d);
            b = c + a;
            mpz_mod(b.get_mpz_t(), b.get_mpz_t(), mod.get_mpz_t());
        } else {
            a.swap(c);
            b.swap(d);
        }
    }

    return {a, b};
}

mpz_class pow_ui(unsigned long base, unsigned long exp) {
    mpz_class result;
    mpz_ui_pow_ui(result.get_mpz_t(), base, exp);
    return result;
}

mpz_class pow2(unsigned long exp) {
    mpz_class result;
    mpz_setbit(result.get_mpz_t(), exp);
    return result;
}

mpz_class pow5(unsigned long exp) {
    constexpr unsigned long kBlockExp = 8;
    constexpr unsigned long kBlockBase = 390625;  // 5^8。GMP に大きな塊で累乗させる。

    mpz_class result;
    mpz_ui_pow_ui(result.get_mpz_t(), kBlockBase, exp / kBlockExp);
    if (const unsigned long rem = exp % kBlockExp; rem != 0) {
        result *= pow_ui(5, rem);
    }
    return result;
}

mpz_class pow_prime(unsigned p, unsigned long exp) {
    return p == 2 ? pow2(exp) : pow5(exp);
}

void multiply_by_p_power(mpz_class& x, unsigned p, unsigned long exp, const mpz_class& mod) {
    if (exp == 0) return;
    if (p == 2) {
        mpz_mul_2exp(x.get_mpz_t(), x.get_mpz_t(), exp);
    } else {
        x *= pow5(exp);
    }
    mpz_mod(x.get_mpz_t(), x.get_mpz_t(), mod.get_mpz_t());
}

unsigned long vp_ui(unsigned long x, unsigned p) {
    unsigned long v = 0;
    while (x % p == 0) {
        x /= p;
        ++v;
    }
    return v;
}

unsigned long strip_p_ui(unsigned long& x, unsigned p) {
    unsigned long v = 0;
    while (x % p == 0) {
        x /= p;
        ++v;
    }
    return v;
}

unsigned long strip_p_mpz(mpz_class& x, unsigned p) {
    unsigned long v = 0;
    while (mpz_divisible_ui_p(x.get_mpz_t(), p)) {
        mpz_divexact_ui(x.get_mpz_t(), x.get_mpz_t(), p);
        ++v;
    }
    return v;
}

unsigned long vp_mpz(mpz_class x, unsigned p) {
    if (x == 0) return static_cast<unsigned long>(-1);
    if (x < 0) x = -x;
    unsigned long v = 0;
    while (mpz_divisible_ui_p(x.get_mpz_t(), p)) {
        mpz_divexact_ui(x.get_mpz_t(), x.get_mpz_t(), p);
        ++v;
    }
    return v;
}

mpz_class pow3_small(unsigned long exp, const mpz_class& mod) {
    mpz_class result;
    mpz_ui_pow_ui(result.get_mpz_t(), 3, exp);
    mpz_mod(result.get_mpz_t(), result.get_mpz_t(), mod.get_mpz_t());
    return result;
}

unsigned long log_reduction_target(unsigned long digits) {
#ifdef PADIC_LOG_REDUCTION_FIXED
    return PADIC_LOG_REDUCTION_FIXED;
#else
    // log(x)=log(x^(p^s))/p^s の形にして log の項数を減らす。
    // target は v_p(x^(p^s)-1) の目標値。実測では sqrt(N) より少し低い
    // 7/8*sqrt(N) くらいがこの環境では安定して速かった。
    unsigned long r = 1;
    while (r <= digits / r) ++r;
    --r;
    r = max(1UL, (r * PADIC_LOG_REDUCTION_NUM) / PADIC_LOG_REDUCTION_DEN);
    if (r < PADIC_LOG_REDUCTION_MIN) return PADIC_LOG_REDUCTION_MIN;
    if (r > PADIC_LOG_REDUCTION_MAX) return PADIC_LOG_REDUCTION_MAX;
    return r;
#endif
}

mpz_class padic_log_1_plus(unsigned p, unsigned long digits, mpz_class u, const mpz_class& mod) {
    // log(1+u)=u-u^2/2+u^3/3-...
    // k に p が含まれる項では割り算で p-adic 精度が落ちるので、
    // 内部では少し余分な桁を持つ。
    const unsigned long work_digits = digits + 64;
    const mpz_class work_mod = pow_prime(p, work_digits);

    mpz_mod(u.get_mpz_t(), u.get_mpz_t(), work_mod.get_mpz_t());
    if (u == 0) return 0;

    const unsigned long u_v = vp_mpz(u, p);
    if ((p == 2 && u_v < 2) || (p != 2 && u_v < 1)) {
        throw invalid_argument("p-adic log argument is outside the convergence radius");
    }

    mpz_class sum = 1;
    sum = 0;

    mpz_class power = 1;
    for (unsigned long k = 1;; ++k) {
        const unsigned long k_v = vp_ui(k, p);
        if (k * u_v < k_v || k * u_v - k_v >= digits) break;

        power *= u;
        mpz_mod(power.get_mpz_t(), power.get_mpz_t(), work_mod.get_mpz_t());

        unsigned long denominator = k;
        strip_p_ui(denominator, p);

        mpz_class term = power;
        for (unsigned long i = 0; i < k_v; ++i) {
            mpz_divexact_ui(term.get_mpz_t(), term.get_mpz_t(), p);
        }

        mpz_class inverse_denominator;
        if (mpz_invert(inverse_denominator.get_mpz_t(),
                       mpz_class(denominator).get_mpz_t(),
                       mod.get_mpz_t()) == 0) {
            throw runtime_error("log denominator is not invertible modulo p^N");
        }
        term *= inverse_denominator;
        mpz_mod(term.get_mpz_t(), term.get_mpz_t(), mod.get_mpz_t());

        if (k & 1UL) {
            sum += term;
        } else {
            sum -= term;
        }
        mpz_mod(sum.get_mpz_t(), sum.get_mpz_t(), mod.get_mpz_t());
    }

    return sum;
}

mpz_class padic_log_unit(unsigned p, unsigned long digits, mpz_class x, const mpz_class& mod) {
    mpz_mod(x.get_mpz_t(), x.get_mpz_t(), mod.get_mpz_t());
    mpz_class u = x - 1;
    mpz_mod(u.get_mpz_t(), u.get_mpz_t(), mod.get_mpz_t());
    if (u > mod / 2) u -= mod;
    if (u == 0) return 0;

    const unsigned long u_v = vp_mpz(u, p);
    if ((p == 2 && u_v < 2) || (p != 2 && u_v < 1)) {
        throw invalid_argument("p-adic log argument is outside the convergence radius");
    }

    const unsigned long target_v = log_reduction_target(digits);
    const unsigned long shifts = target_v > u_v ? target_v - u_v : 0;
    if (shifts == 0) {
        return padic_log_1_plus(p, digits, u, mod);
    }

    const unsigned long work_digits = digits + shifts;
    const mpz_class work_mod = pow_prime(p, work_digits);
    mpz_class lifted = x;
    mpz_mod(lifted.get_mpz_t(), lifted.get_mpz_t(), work_mod.get_mpz_t());

    // x を p 乗するたびに、だいたい v_p(x-1) が 1 上がる。
    // log(x^(p^s)) = p^s log(x) なので、最後に p^s で割り戻す。
    for (unsigned long i = 0; i < shifts; ++i) {
        if (p == 2) {
            lifted *= lifted;
            mpz_mod(lifted.get_mpz_t(), lifted.get_mpz_t(), work_mod.get_mpz_t());
        } else {
            mpz_powm_ui(lifted.get_mpz_t(), lifted.get_mpz_t(), p, work_mod.get_mpz_t());
        }
    }

    mpz_class reduced_u = lifted - 1;
    mpz_mod(reduced_u.get_mpz_t(), reduced_u.get_mpz_t(), work_mod.get_mpz_t());
    if (reduced_u > work_mod / 2) reduced_u -= work_mod;

    mpz_class high_log = padic_log_1_plus(p, work_digits, reduced_u, work_mod);
    if (high_log > work_mod / 2) high_log -= work_mod;
    for (unsigned long i = 0; i < shifts; ++i) {
        mpz_divexact_ui(high_log.get_mpz_t(), high_log.get_mpz_t(), p);
    }

    mpz_mod(high_log.get_mpz_t(), high_log.get_mpz_t(), mod.get_mpz_t());
    return high_log;
}

mpz_class padic_exp_series(unsigned p, unsigned long digits, mpz_class y, const mpz_class& mod) {
    // exp(y)=1+y+y^2/2!+...
    // Newton の補正 delta は高い p-adic valuation を持つので、
    // ここは補正項だけを級数で短く計算する用途。
    mpz_mod(y.get_mpz_t(), y.get_mpz_t(), mod.get_mpz_t());
    if (y > mod / 2) y -= mod;
    if (y == 0) return 1;

    const unsigned long y_v = strip_p_mpz(y, p);
    if ((p == 2 && y_v < 2) || (p != 2 && y_v < 1)) {
        throw invalid_argument("p-adic exp series argument is outside the convergence radius");
    }

    mpz_class sum = 1;
    mpz_class term_unit = 1;
    unsigned long term_v = 0;
    mpz_class p_power = 1;
    unsigned long p_power_v = 0;

    mpz_mod(y.get_mpz_t(), y.get_mpz_t(), mod.get_mpz_t());
    for (unsigned long k = 1; term_v < digits; ++k) {
        unsigned long denominator = k;
        const unsigned long denominator_v = strip_p_ui(denominator, p);

        term_v += y_v;
        if (term_v < denominator_v) {
            throw runtime_error("unexpected negative exp term valuation");
        }
        term_v -= denominator_v;
        if (term_v >= digits) break;

        term_unit *= y;

        mpz_class inverse_denominator;
        if (mpz_invert(inverse_denominator.get_mpz_t(),
                       mpz_class(denominator).get_mpz_t(),
                       mod.get_mpz_t()) == 0) {
            throw runtime_error("exp denominator is not invertible modulo p^N");
        }
        term_unit *= inverse_denominator;
        mpz_mod(term_unit.get_mpz_t(), term_unit.get_mpz_t(), mod.get_mpz_t());

        // p^term_v を毎回作り直さず、前項との差分だけ掛ける。
        multiply_by_p_power(p_power, p, term_v - p_power_v, mod);
        p_power_v = term_v;
        mpz_class contribution = term_unit * p_power;
        mpz_mod(contribution.get_mpz_t(), contribution.get_mpz_t(), mod.get_mpz_t());
        sum += contribution;
        mpz_mod(sum.get_mpz_t(), sum.get_mpz_t(), mod.get_mpz_t());
    }

    return sum;
}

mpz_class padic_exp_newton(unsigned p, unsigned long digits, const mpz_class& y) {
    const unsigned long y_v = vp_mpz(y, p);
    if ((p == 2 && y_v < 2) || (p != 2 && y_v < 1)) {
        throw invalid_argument("p-adic exp argument is outside the convergence radius");
    }

    mpz_class z = 1;
    for (unsigned long precision = 1; precision < digits;) {
        const unsigned long next_precision = min(digits, max(precision + 1, precision * 2));
        const mpz_class mod = pow_prime(p, next_precision);

        mpz_mod(z.get_mpz_t(), z.get_mpz_t(), mod.get_mpz_t());

        mpz_class log_z = padic_log_unit(p, next_precision, z, mod);
        mpz_class delta = y - log_z;
        mpz_mod(delta.get_mpz_t(), delta.get_mpz_t(), mod.get_mpz_t());

        // Newton 更新:
        //   log(z)=y を解きたい。
        //   z_new = z * exp(y-log(z))
        // これで正しい p-adic 桁数が倍々に増える。
        mpz_class correction = padic_exp_series(p, next_precision, delta, mod);
        z *= correction;
        mpz_mod(z.get_mpz_t(), z.get_mpz_t(), mod.get_mpz_t());

        precision = next_precision;
    }

    const mpz_class mod = pow_prime(p, digits);
    mpz_mod(z.get_mpz_t(), z.get_mpz_t(), mod.get_mpz_t());
    return z;
}

mpz_class pow3_mod_prime_power_padic(unsigned p,
                                     unsigned long digits,
                                     const mpz_class& exponent,
                                     const mpz_class& mod) {
    const unsigned long order_unit = (p == 2) ? 2 : 4;
    const unsigned long r = mpz_fdiv_ui(exponent.get_mpz_t(), order_unit);
    mpz_class q = exponent - r;
    mpz_divexact_ui(q.get_mpz_t(), q.get_mpz_t(), order_unit);

    const unsigned long principal_unit = (p == 2) ? 9 : 81;
    // 収束条件:
    //   p=2: 9 = 1 + 8 は 1 + 4Z_2 に入るので log(9) が収束する。
    //        q*log(9) も v2 >= 3 なので exp も収束する。
    //   p=5: 81 = 1 + 80 は 1 + 5Z_5 に入るので log(81) が収束する。
    //        q*log(81) も v5 >= 1 なので exp も収束する。
    const mpz_class log_unit = padic_log_unit(p, digits, principal_unit, mod);

    mpz_class y = q * log_unit;
    mpz_mod(y.get_mpz_t(), y.get_mpz_t(), mod.get_mpz_t());

    mpz_class result = padic_exp_newton(p, digits, y);
    result *= pow3_small(r, mod);
    mpz_mod(result.get_mpz_t(), result.get_mpz_t(), mod.get_mpz_t());
    return result;
}

struct PrimePowerSide {
    mpz_class mod;
    mpz_class residue;
};

PrimePowerSide compute_side(unsigned p, uint64_t fib_index, unsigned long digits) {
    PrimePowerSide side;
    if (p == 2) {
        side.mod = pow2(digits);
        // 奇数底の mod 2^N で指数は lambda(2^N)=2^(N-2) で落とせる。
        const mpz_class mod_exp = pow2(digits - 2);
        const mpz_class exponent = fib_mod_iter(fib_index, mod_exp).first;
        side.residue = pow3_mod_prime_power_padic(2, digits, exponent, side.mod);
    } else if (p == 5) {
        side.mod = pow5(digits);
        // 奇数かつ 5 と互いに素な底では lambda(5^N)=4*5^(N-1)。
        mpz_class mod_exp = pow5(digits - 1);
        mod_exp *= 4;
        const mpz_class exponent = fib_mod_iter(fib_index, mod_exp).first;
        side.residue = pow3_mod_prime_power_padic(5, digits, exponent, side.mod);
    } else {
        throw invalid_argument("only p=2 and p=5 are supported");
    }
    return side;
}

void print_usage(const char* argv0) {
    cerr << "usage: " << argv0 << " <fib_index> <decimal_digits> [--quiet]\n";
    cerr << "   or: " << argv0 << " 3 <fib_index> <decimal_digits> [--quiet]\n";
}

}  // namespace

int main(int argc, char** argv) {
    cout << "hello" << endl;
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    uint64_t base = 3;
    uint64_t fib_index = 0;
    unsigned long digits = 0;
    bool print_result = true;

    if (argc == 3) {
        fib_index = stoull(argv[1]);
        digits = stoul(argv[2]);
    } else if (argc == 4) {
        const string third = argv[3];
        if (third == "--quiet") {
            fib_index = stoull(argv[1]);
            digits = stoul(argv[2]);
            print_result = false;
        } else {
            base = stoull(argv[1]);
            fib_index = stoull(argv[2]);
            digits = stoul(argv[3]);
        }
    } else if (argc == 5) {
        base = stoull(argv[1]);
        fib_index = stoull(argv[2]);
        digits = stoul(argv[3]);
        const string flag = argv[4];
        if (flag != "--quiet") {
            print_usage(argv[0]);
            return 1;
        }
        print_result = false;
    } else {
        if (!(cin >> base >> fib_index >> digits)) {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (base != 3) {
        throw invalid_argument("fibonacci_padic.cpp currently supports only base 3");
    }
    if (digits < 4) {
        throw invalid_argument("digits must be at least 4");
    }

    cout << "calc 3^fib(" << fib_index << ") mod 10^" << digits << " by p-adic log/exp Newton\n";

    const auto start = chrono::high_resolution_clock::now();

#if PADIC_PARALLEL
    // 2^N 側と 5^N 側は独立なので並列に計算する。
    auto future2 = async(launch::async, compute_side, 2, fib_index, digits);
    auto future5 = async(launch::async, compute_side, 5, fib_index, digits);

    const PrimePowerSide side2 = future2.get();
    const PrimePowerSide side5 = future5.get();
#else
    const PrimePowerSide side2 = compute_side(2, fib_index, digits);
    const PrimePowerSide side5 = compute_side(5, fib_index, digits);
#endif
    const mpz_class& mod2 = side2.mod;
    const mpz_class& mod5 = side5.mod;
    const mpz_class& r2 = side2.residue;
    const mpz_class& r5 = side5.residue;

    mpz_class inv5_mod2;
    if (mpz_invert(inv5_mod2.get_mpz_t(), mod5.get_mpz_t(), mod2.get_mpz_t()) == 0) {
        throw runtime_error("CRT inverse does not exist");
    }

    mpz_class t = r2 - r5;
    mpz_mod(t.get_mpz_t(), t.get_mpz_t(), mod2.get_mpz_t());
    t *= inv5_mod2;
    mpz_mod(t.get_mpz_t(), t.get_mpz_t(), mod2.get_mpz_t());

    // CRT:
    //   x = r5 + 5^N t
    //   t = (r2-r5) * (5^N)^(-1) mod 2^N
    mpz_class result = r5 + mod5 * t;

    const auto end = chrono::high_resolution_clock::now();
    const auto duration_us = chrono::duration_cast<chrono::microseconds>(end - start).count();

    if (print_result) cout << result << '\n';
    cout << "elapsed_time = " << duration_us / 1000.0 << " ms\n";
}
