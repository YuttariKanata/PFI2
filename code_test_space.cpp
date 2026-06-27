#include "PFI2.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <gmp.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h> // __rdtsc() 用
#define READ_TSC() __rdtsc()
#else
#define READ_TSC() 0
#endif

// 最適化によるデッドコード消去を防ぐためのバリア
inline void keep_alive_barrier(void* ptr) noexcept {
#if defined(_MSC_VER)
    char* keep = static_cast<char*>(ptr);
    (void)*keep;
#else
    asm volatile("" : : "g"(ptr) : "memory");
#endif
}

// ベンチマーク用の1ペア構造体
struct GMPBenchPair {
    mpz_t gmp_a;
    mpz_t gmp_b;
    mpz_t gmp_a_backup;
};

struct PFIBenchPair {
    PFI2 pfi_a;
    PFI2 pfi_b;
    PFI2 pfi_a_backup;

    PFIBenchPair(std::size_t max_words) 
        : pfi_a(max_words * 3), pfi_b(max_words * 3), pfi_a_backup(max_words * 3) {}
};

// コンテキスト管理クラス：データセットの生成とライフサイクルを統括
class BenchmarkContext {
public:
    std::vector<GMPBenchPair> gmp_dataset;
    std::vector<PFIBenchPair> pfi_dataset;
    std::size_t num_pairs;
    std::size_t test_max_words;

    BenchmarkContext(std::size_t num_pairs, std::size_t test_max_words)
        : num_pairs(num_pairs), test_max_words(test_max_words) {}

    ~BenchmarkContext() {
        for (auto& pair : gmp_dataset) {
            mpz_clear(pair.gmp_a);
            mpz_clear(pair.gmp_b);
            mpz_clear(pair.gmp_a_backup);
        }
    }

    // 減算ベンチマーク用に、確実に A >= B を満たすデータセットを生成する
    void generate_sub_safe_dataset(uint64_t seed) {
        std::mt19937_64 eng(seed);
        gmp_randstate_t gmp_state;
        gmp_randinit_default(gmp_state);
        gmp_randseed_ui(gmp_state, seed);

        const unsigned long BITS_PER_WORD = static_cast<unsigned long>(PFI2::DIGITS_PER_WORD * std::log2(PFI2::BASE) * 0.8);
        const unsigned long MAX_SAFE_BITS = test_max_words * BITS_PER_WORD;
        std::uniform_int_distribution<unsigned long> bit_dist(1, MAX_SAFE_BITS);

        gmp_dataset.reserve(num_pairs);
        pfi_dataset.reserve(num_pairs);

        for (std::size_t i = 0; i < num_pairs; ++i) {
            GMPBenchPair gmp_pair;
            mpz_init(gmp_pair.gmp_a);
            mpz_init(gmp_pair.gmp_b);
            mpz_init(gmp_pair.gmp_a_backup);

            unsigned long bits_a = bit_dist(eng) % MAX_SAFE_BITS + 1;
            unsigned long bits_b = bit_dist(eng) % MAX_SAFE_BITS + 1;

            mpz_urandomb(gmp_pair.gmp_a, gmp_state, bits_a);
            mpz_urandomb(gmp_pair.gmp_b, gmp_state, bits_b);

            // 絶対値に固定
            mpz_abs(gmp_pair.gmp_a, gmp_pair.gmp_a);
            mpz_abs(gmp_pair.gmp_b, gmp_pair.gmp_b);

            // 【最重要】減算カーネルをいじめるため、A >= B にトポロジーを制御
            if (mpz_cmp(gmp_pair.gmp_a, gmp_pair.gmp_b) < 0) {
                mpz_swap(gmp_pair.gmp_a, gmp_pair.gmp_b);
            }

            // エッジケース注入（10% の確率で A == B に倒し、完全に 0 へのコラプスを発生させる）
            if (i % 10 == 0) {
                mpz_set(gmp_pair.gmp_b, gmp_pair.gmp_a);
            } else if (i % 10 == 1) {
                mpz_set_ui(gmp_pair.gmp_b, 0); // B = 0 ルート
            }

            mpz_set(gmp_pair.gmp_a_backup, gmp_pair.gmp_a);
            gmp_dataset.push_back(gmp_pair);

            // PFI2 側へインポート
            pfi_dataset.emplace_back(test_max_words);
            pfi_dataset[i].pfi_a.from_mpz(gmp_pair.gmp_a);
            pfi_dataset[i].pfi_b.from_mpz(gmp_pair.gmp_b);
            pfi_dataset[i].pfi_a_backup.from_mpz(gmp_pair.gmp_a_backup);
        }

        gmp_randclear(gmp_state);
    }

    // 従来の加算用データセット生成
    void generate_add_dataset(uint64_t seed) {
        std::mt19937_64 eng(seed);
        gmp_randstate_t gmp_state;
        gmp_randinit_default(gmp_state);
        gmp_randseed_ui(gmp_state, seed);

        const unsigned long BITS_PER_WORD = static_cast<unsigned long>(PFI2::DIGITS_PER_WORD * std::log2(PFI2::BASE) * 0.8);
        const unsigned long MAX_SAFE_BITS = test_max_words * BITS_PER_WORD;
        std::uniform_int_distribution<unsigned long> bit_dist(1, MAX_SAFE_BITS);

        gmp_dataset.reserve(num_pairs);
        pfi_dataset.reserve(num_pairs);

        for (std::size_t i = 0; i < num_pairs; ++i) {
            GMPBenchPair gmp_pair;
            mpz_init(gmp_pair.gmp_a);
            mpz_init(gmp_pair.gmp_b);
            mpz_init(gmp_pair.gmp_a_backup);

            mpz_urandomb(gmp_pair.gmp_a, gmp_state, bit_dist(eng) % MAX_SAFE_BITS + 1);
            mpz_urandomb(gmp_pair.gmp_b, gmp_state, bit_dist(eng) % MAX_SAFE_BITS + 1);

            if (i % 10 == 0) {
                mpz_set_ui(gmp_pair.gmp_b, 0);
            } else if (i % 10 == 1) {
                mpz_urandomb(gmp_pair.gmp_b, gmp_state, bit_dist(eng) % 100 + 1);
            }

            mpz_abs(gmp_pair.gmp_a, gmp_pair.gmp_a);
            mpz_abs(gmp_pair.gmp_b, gmp_pair.gmp_b);

            mpz_set(gmp_pair.gmp_a_backup, gmp_pair.gmp_a);
            gmp_dataset.push_back(gmp_pair);

            pfi_dataset.emplace_back(test_max_words);
            pfi_dataset[i].pfi_a.from_mpz(gmp_pair.gmp_a);
            pfi_dataset[i].pfi_b.from_mpz(gmp_pair.gmp_b);
            pfi_dataset[i].pfi_a_backup.from_mpz(gmp_pair.gmp_a_backup);
        }

        gmp_randclear(gmp_state);
    }

    // 演算ループ終了ごとに状態を完全にリセット
    inline void reset_states() noexcept {
        for (std::size_t i = 0; i < num_pairs; ++i) {
            mpz_set(gmp_dataset[i].gmp_a, gmp_dataset[i].gmp_a_backup);
            pfi_dataset[i].pfi_a.copy_from(pfi_dataset[i].pfi_a_backup);
        }
    }
};

// ----------------------------------------------------------------
// 各カーネルの測定実行関数（高階関数トポロジー）
// ----------------------------------------------------------------
template <typename GMPFunc, typename PFIFunc>
void execute_benchmark(const std::string& label, BenchmarkContext& ctx, std::size_t inner_loops, GMPFunc gmp_op, PFIFunc pfi_op) {
    std::cout << "--- Starting Benchmark: " << label << " (" << inner_loops << " loops) ---\n";
    
    // キャッシュウォームアップ
    for (std::size_t i = 0; i < ctx.num_pairs; ++i) {
        gmp_op(ctx.gmp_dataset[i].gmp_a, ctx.gmp_dataset[i].gmp_b);
        pfi_op(ctx.pfi_dataset[i].pfi_a, ctx.pfi_dataset[i].pfi_b);
    }
    ctx.reset_states();

    // 1. GMP 測定区間
    auto gmp_start = std::chrono::high_resolution_clock::now();
    for (std::size_t loop = 0; loop < inner_loops; ++loop) {
        for (std::size_t i = 0; i < ctx.num_pairs; ++i) {
            gmp_op(ctx.gmp_dataset[i].gmp_a, ctx.gmp_dataset[i].gmp_b);
            mpz_set(ctx.gmp_dataset[i].gmp_a, ctx.gmp_dataset[i].gmp_a_backup); // 高速メモリ復帰
            keep_alive_barrier(&ctx.gmp_dataset[i].gmp_a);
        }
    }
    auto gmp_end = std::chrono::high_resolution_clock::now();
    double gmp_total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(gmp_end - gmp_start).count();

    // 2. PFI2 測定区間（TSC精密プロファイリング付き）
    uint64_t total_kernel_cycles = 0;
    uint64_t total_copy_cycles = 0;

    auto pfi_start = std::chrono::high_resolution_clock::now();
    for (std::size_t loop = 0; loop < inner_loops; ++loop) {
        for (std::size_t i = 0; i < ctx.num_pairs; ++i) {
            
            uint64_t t0 = READ_TSC();
            pfi_op(ctx.pfi_dataset[i].pfi_a, ctx.pfi_dataset[i].pfi_b); // ターゲットカーネル実行
            uint64_t t1 = READ_TSC();
            
            ctx.pfi_dataset[i].pfi_a.copy_from(ctx.pfi_dataset[i].pfi_a_backup); // 復帰
            uint64_t t2 = READ_TSC();

            total_kernel_cycles += (t1 - t0);
            total_copy_cycles += (t2 - t1);
            
            keep_alive_barrier(&ctx.pfi_dataset[i].pfi_a);
        }
    }
    auto pfi_end = std::chrono::high_resolution_clock::now();
    double pfi_total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(pfi_end - pfi_start).count();

    // 結果出力
    std::size_t total_ops = ctx.num_pairs * inner_loops;
    double gmp_ns_per_op = gmp_total_ns / total_ops;
    double pfi_ns_per_op = pfi_total_ns / total_ops;
    double ratio = gmp_ns_per_op / pfi_ns_per_op;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  Total Operations  : " << total_ops << "\n";
    std::cout << "  GMP (Op + Copy)   : " << gmp_ns_per_op << " ns/op\n";
    std::cout << "  PFI2 (Op + Copy)  : " << pfi_ns_per_op << " ns/op\n";
    std::cout << "  -------------------------\n";
    if (ratio >= 1.0) {
        std::cout << "  Result: PFI2 is " << ratio << "x FASTER than GMP!\n";
    } else {
        std::cout << "  Result: PFI2 is " << (1.0 / ratio) << "x SLOWER than GMP.\n";
    }
    std::cout << "  -------------------------\n";
    std::cout << "  [PFI2 Low-Level TSC Profile]\n";
    std::cout << "    Pure Kernel : " << (double)total_kernel_cycles / total_ops << " cycles/op\n";
    std::cout << "    Pure Copy   : " << (double)total_copy_cycles / total_ops << " cycles/op\n";
    std::cout << "=========================================================\n\n";
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "     PFI2 vs GMP Pure Inplace Speed Test (NO RADIX CONV) \n";
    std::cout << "=========================================================\n";

    const std::size_t TEST_MAX_WORDS = 300;     
    const std::size_t NUM_PAIRS = 200;         
    const std::size_t INNER_LOOPS = 50000;      
    const uint64_t RUN_SEED = 0x59604644;

    // ----------------------------------------------------------------
    // 1. Absolute ADD Benchmark
    // ----------------------------------------------------------------
    {
        BenchmarkContext add_ctx(NUM_PAIRS, TEST_MAX_WORDS);
        add_ctx.generate_add_dataset(RUN_SEED);

        // ラムダ式でインプレース加算を束縛して投入
        execute_benchmark(
            "Absolute ADD (add_abs_inplace)", 
            add_ctx, 
            INNER_LOOPS,
            [](mpz_t a, const mpz_t b) { mpz_add(a, a, b); },
            [](PFI2& a, const PFI2& b) { a.add_abs_inplace(b); }
        );
    }

    // ----------------------------------------------------------------
    // 2. Absolute SUB Benchmark
    // ----------------------------------------------------------------
    {
        BenchmarkContext sub_ctx(NUM_PAIRS, TEST_MAX_WORDS);
        // 減算用に A >= B 保証トポロジーでデータを生成
        sub_ctx.generate_sub_safe_dataset(RUN_SEED);

        // ラムダ式でインプレース減算を束縛して投入
        execute_benchmark(
            "Absolute SUB (sub_abs_inplace)", 
            sub_ctx, 
            INNER_LOOPS,
            [](mpz_t a, const mpz_t b) { mpz_sub(a, a, b); },
            [](PFI2& a, const PFI2& b) { a.sub_abs_inplace(b); }
        );
    }

    return 0;
}