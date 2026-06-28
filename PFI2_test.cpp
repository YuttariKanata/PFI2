#include "PFI2.h"
#include <iostream>
#include <cassert>
#include <random>
#include <vector>
#include <gmp.h>

void test_constructor_and_move() {
    std::cout << "Running: Test Constructor and Move..." << std::endl;

    // 1. 通常コンストラクト（メモリは未初期化だが active_words = 0, is_zero() == true）
    PFI2 a(100);
    assert(a.invariant_holds());
    assert(a.is_zero());
    assert(a.get_digit(0) == 0);
    assert(a.get_digit(1) == 0);
    assert(a.get_digit(99) == 0);

    // 2. 値をセットして状態を変化させる
    bool ok = a.set_digit(5, 12345);
    assert(ok);
    assert(a.invariant_holds());
    assert(!a.is_zero());
    assert(a.get_digit(5) == 12345);
    assert(a.get_digit(4) == 0);
    assert(a.get_digit(3) == 0);
    assert(a.get_digit(2) == 0);
    assert(a.get_digit(1) == 0);
    assert(a.get_digit(0) == 0);

    // 3. ムーブコンストラクタの検証
    PFI2 b(std::move(a));
    assert(b.invariant_holds());
    assert(!b.is_zero());
    assert(b.get_digit(5) == 12345);
    assert(b.get_digit(4) == 0);
    assert(b.get_digit(3) == 0);
    assert(b.get_digit(2) == 0);
    assert(b.get_digit(1) == 0);
    assert(b.get_digit(0) == 0);
    
    // 移動元の a は安全に初期化されている必要がある
    assert(a.is_zero()); 

    // 4. ムーブ代入の検証
    PFI2 c(50);
    c = std::move(b);
    assert(c.invariant_holds());
    assert(c.get_digit(5) == 12345);
    assert(c.get_digit(4) == 0);
    assert(c.get_digit(3) == 0);
    assert(c.get_digit(2) == 0);
    assert(c.get_digit(1) == 0);
    assert(c.get_digit(0) == 0);
    assert(b.is_zero());

    std::cout << "-> Passed!" << std::endl;
}

void test_clear_and_zero() {
    std::cout << "Running: Test Clear and Zero..." << std::endl;

    PFI2 a(100);
    // 巨大なジャンプ書き込みをして、隙間に生ゴミを内包させる
    bool ok = a.set_digit(20, 99);
    assert(ok);
    assert(!a.is_zero());
    assert(a.invariant_holds());

    // clear() は O(1) でシャッターを閉めるだけ
    a.clear();
    assert(a.is_zero());
    assert(a.invariant_holds()); // 有効サイズ 0 なので、words_内がゴミだらけでも不変条件をパスする

    // クリア後に再度読み出しても、上位ゼロ拡張の契約により 0 が返るか
    assert(a.get_digit(20) == 0);
    assert(a.get_digit(0)  == 0);
    assert(a.get_digit(1)  == 0);
    assert(a.get_digit(50) == 0);

    std::cout << "-> Passed!" << std::endl;
}

void test_validation_and_bounds() {
    std::cout << "Running: Test Validation and Bounds..." << std::endl;

    PFI2 a(10); // DIGITS_PER_WORD = 3 なので、4ワード (12桁分) 確保される words_[0] ~ words_[3]

    // 1. BASE（390625）以上の不正入力を弾くか
    bool ok1 = a.set_digit(0, PFI2::BASE);
    assert(!ok1); // 拒否
    assert(a.invariant_holds());

    bool ok2 = a.set_digit(0, PFI2::BASE + 999);
    assert(!ok2); // 拒否
    assert(a.invariant_holds());

    // 2. 物理容量（4ワード = 12桁）を超えるインデックスへの書き込みを弾くか
    // 12桁目（0-indexed で index=12）は 5番目のワードwords_[4]になるためキャパ越え
    bool ok3 = a.set_digit(12, 1);
    assert(!ok3); // 拒否
    assert(a.invariant_holds());

    // 境界ギリギリの index=11（4ワード目の最終パート）への書き込みは成功するはず (あまりするべきでない)
    bool ok4 = a.set_digit(11, 1);
    assert(ok4);
    assert(a.invariant_holds());

    // BASE未満であれば入力される
    bool ok5 = a.set_digit(7, PFI2::BASE - 1);
    assert(ok5);
    assert(a.invariant_holds());

    std::cout << "-> Passed!" << std::endl;
}

void test_delayed_initialization_and_zero_collapse() {
    std::cout << "Running: Test Delayed Initialization and Zero Collapse..." << std::endl;

    PFI2 a(30); // 10ワード分 ( words_[0] ~ words_[9] )
    
    // 1. 飛び地への書き込み（遅延初期化の誘発）
    bool ok = a.set_digit(12, 555); 
    assert(ok);
    assert(a.invariant_holds());
    assert(a.get_digit(12) == 555);
    
    // 飛び地の隙間（0〜11桁）が正しく 0 扱いになっているか検証
    for (std::size_t i = 0; i < 12; ++i) {
        assert(a.get_digit(i) == 0);
    }

    // 2. 同じワードの別パートへの書き込み（mask と結合の検証）
    ok = a.set_digit(13, 777);
    assert(ok);
    assert(a.invariant_holds());
    assert(a.get_digit(12) == 555);
    assert(a.get_digit(13) == 777);

    // 3. ゼロ収束による収縮（update_active_words のインライン実装の検証）
    ok = a.set_digit(13, 0);
    assert(ok);
    assert(a.invariant_holds());
    assert(a.get_digit(13) == 0);
    assert(a.get_digit(12) == 555); // まだ 12 が残っているので active_words は維持

    // 最後の砦である index=12 も 0 に落とす。
    ok = a.set_digit(12, 0);
    assert(ok);
    assert(a.invariant_holds());
    assert(a.is_zero()); // 完全に 0 に戻ったか

    std::cout << "-> Passed!" << std::endl;
}

// 実行ごとにシードが変わる完全にランダムなファジング（ストレステスト）
void run_random_fuzz_test(std::size_t iterations) {
    std::cout << "Running: Random Fuzz Test (" << iterations << " iterations)..." << std::endl;

    // 実行ごとに異なるシード値を生成。バグ発生時に再現できるよう、シードを標準出力にダンプする
    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);
    std::cout << "   [Fuzz Seed]: 0x" << std::hex << seed << std::dec << std::endl;

    const std::size_t TEST_MAX_DIGITS = 300; // 約100ワード
    PFI2 target(TEST_MAX_DIGITS);

    // 状態追跡用のシャドーバッファ。未初期化空間を許容する PFI2 に対し、本来あるべき「真の値」を完全シミュレート
    std::vector<uint64_t> shadow(TEST_MAX_DIGITS, 0);

    // 各種操作（0:書き込み, 1:読み出し, 2:一括クリア, 3:ピンポイントゼロ化）を等確率で選択
    std::uniform_int_distribution<int> op_dist(0, 4);
    // 意図的に物理容量をハミ出すインデックス（TEST_MAX_DIGITS以上）も生成し、ガードレイルの堅牢性を検証
    std::uniform_int_distribution<std::size_t> idx_dist(0, TEST_MAX_DIGITS + 10);
    // 意図的に BASE 以上の不正な値を生成し、内部不変条件（各パート < BASE）の防御力を検証
    std::uniform_int_distribution<uint64_t> val_dist(0, PFI2::BASE + 10000);

    for (std::size_t i = 0; i < iterations; ++i) {
        int op = op_dist(eng);
        std::size_t idx = idx_dist(eng);

        if (op == 0) {
            // --- 操作 0: 通常値のセット、またはジャンプ書き込みによる遅延初期化の誘発 ---
            uint64_t val = val_dist(eng);
            bool res = target.set_digit(idx, val);

            // インデックスから、対応する内部物理ワードの割り当て位置を算出
            std::size_t expected_word_idx = idx / PFI2::DIGITS_PER_WORD;
            std::size_t max_capacity_words = (TEST_MAX_DIGITS + PFI2::DIGITS_PER_WORD - 1) / PFI2::DIGITS_PER_WORD;

            // 入力値が不正（>= BASE）、または物理容量上限を超えている場合は、不変条件を壊さず安全に弾かれなければならない
            if (val >= PFI2::BASE || expected_word_idx >= max_capacity_words) {
                assert(!res);
            } else {
                // 正常な書き込み。飛び先より下の未初期化領域は、set_digit内部で安全に0遅延初期化されているはず
                assert(res);
                shadow[idx] = val; // シャドーバッファ（期待値）を同期
            }
        } 
        else if (op == 1) {
            // --- 操作 1: ゲッターの整合性、および上位ゼロ拡張の検証 ---
            uint64_t actual_val = target.get_digit(idx);
            if (idx >= TEST_MAX_DIGITS) {
                // 物理仕様上の範囲外、または active_words_ を超えた領域からは、メモリロードせずに即座に 0 が返る契約
                assert(actual_val == 0);
            } else {
                // 有効範囲内のデータが、生ゴミに足元を掬われることなく期待値と完全一致するかを検証
                assert(actual_val == shadow[idx]);
            }
        } 
        else if (op == 2) {
            // --- 操作 2: O(1)クリアによる、内部メモリの「生ゴミ化」のシミュレート ---
            target.clear();
            std::fill(shadow.begin(), shadow.end(), 0);
            assert(target.is_zero());
            // この直後、物理配列 words_ には「過去の計算のゴミ」が残ったまま active_words_=0 になる。
            // 次ステップ以降、そのゴミ地帯への再アクセスやジャンプ書き込みが発生し、極めて苛烈なテストケースとなる
        }
        else if (op >= 3) {
            // --- 操作 3: 0への書き換えによる、最上位ワードのゼロ収束（トリム・メモリストア回避）のいびり ---
            if (idx < TEST_MAX_DIGITS) {
                bool res = target.set_digit(idx, 0);
                assert(res);
                shadow[idx] = 0;
                // もし idx が「現在の最上位ワードの最後の非ゼロ要素」だった場合、
                // インライン化したバックスキャンが発動し、active_words_ が安全かつ正しく収縮しているかを追及する
            }
        }

        // 毎ステップごとに、クラスの持つすべての内部不変条件（符号整合、4bit残余のゼロ、各桁<BASE、最上位非ゼロ）を徹底検証
        if (!target.invariant_holds()) {
            std::cerr << "CRITICAL FAILURE at iteration " << i << std::endl;
            std::exit(1);
        }
    }

    std::cout << "-> Passed Fuzzing successfully!" << std::endl;
}


// --- ここまで検証済み(2026年6月6日17時34分) --- //


// 2つのシャドーバッファ（std::vector）を、最上位桁から比較して絶対値の大小をシミュレートするヘルパー
int simulate_shadow_cmp_abs(const std::vector<uint64_t>& lhs, const std::vector<uint64_t>& rhs) {
    // 桁位置の大きい（上の方の）インデックスから下に向かってスキャン
    std::size_t max_len = std::max(lhs.size(), rhs.size());
    for (std::size_t i = max_len; i > 0; --i) {
        std::size_t idx = i - 1;
        uint64_t l_val = (idx < lhs.size()) ? lhs[idx] : 0;
        uint64_t r_val = (idx < rhs.size()) ? rhs[idx] : 0;
        
        if (l_val > r_val) return 1;
        if (l_val < r_val) return -1;
    }
    return 0;
}

void run_cmp_abs_fuzz_test(std::size_t iterations) {
    std::cout << "Running: cmp_abs Random Fuzz Test (" << iterations << " iterations)..." << std::endl;

    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);
    std::cout << "   [cmp_abs Fuzz Seed]: 0x" << std::hex << seed << std::dec << std::endl;

    const std::size_t TEST_MAX_DIGITS = 200;
    
    // 比較検証用に 2 つの独立したインスタンスを生成
    PFI2 pfi_a(TEST_MAX_DIGITS);
    PFI2 pfi_b(TEST_MAX_DIGITS);

    // それぞれの期待値を追跡するシャドーバッファ
    std::vector<uint64_t> shadow_a(TEST_MAX_DIGITS, 0);
    std::vector<uint64_t> shadow_b(TEST_MAX_DIGITS, 0);

    std::uniform_int_distribution<int> op_dist(0, 2); // 0: Aを弄る, 1: Bを弄る, 2: 比較検証
    std::uniform_int_distribution<std::size_t> idx_dist(0, TEST_MAX_DIGITS - 1);
    std::uniform_int_distribution<uint64_t> val_dist(0, PFI2::BASE - 1);

    for (std::size_t i = 0; i < iterations; ++i) {
        int op = op_dist(eng);
        std::size_t idx = idx_dist(eng);

        if (op == 0) {
            // オブジェクトAに対するランダムな書き込み（0上書きによる収縮も含む）
            uint64_t val = (eng() % 5 == 0) ? 0 : val_dist(eng); // 20%の確率で0をねじ込んでトリムをいじめる
            bool res = pfi_a.set_digit(idx, val);
            assert(res);
            shadow_a[idx] = val;
        } 
        else if (op == 1) {
            // オブジェクトBに対するランダムな書き込み
            uint64_t val = (eng() % 5 == 0) ? 0 : val_dist(eng);
            bool res = pfi_b.set_digit(idx, val);
            assert(res);
            shadow_b[idx] = val;
        } 
        else if (op == 2) {
            // --- 比較検証フェーズ ---
            // 1. シャドーバッファ（真の期待値）から純粋な数学的トポロジー比較を算出
            int expected_res = simulate_shadow_cmp_abs(shadow_a, shadow_b);

            // 2. 新仕様の cmp_abs を走らせる（O(1)ショートパス、レジスタサイズ比較、バックスキャンを通過）
            int actual_res_ab = pfi_a.cmp_abs(pfi_b);
            int actual_res_ba = pfi_b.cmp_abs(pfi_a); // 対称性の検証用

            // 3. 期待値との完全一致を徹底追及
            assert(actual_res_ab == expected_res);
            assert(actual_res_ba == -expected_res); // A > B なら B < A でなければならない

            // 4. 自分自身との比較は常に 0 (完全一致) になるか
            assert(pfi_a.cmp_abs(pfi_a) == 0);
            assert(pfi_b.cmp_abs(pfi_b) == 0);
        }

        // 破壊的なメモリ汚染が起きていないか、毎ステップ検証
        if (!pfi_a.invariant_holds() || !pfi_b.invariant_holds()) {
            std::cerr << "CRITICAL FAILURE: Invariant broken during cmp_abs fuzzing at step " << i << std::endl;
            std::exit(1);
        }
    }

    std::cout << "-> Passed cmp_abs Fuzzing successfully!" << std::endl;
}


// --- ここまで検証済み(2026年6月6日17時35分) --- //


void run_gmp_differential_cmp_test(std::size_t iterations) {
    std::cout << "Running: GMP Differential cmp Test (" << iterations << " iterations)..." << std::endl;

    // 乱数生成器の初期化
    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);
    std::cout << "   [GMP Fuzz Seed]: 0x" << std::hex << seed << std::dec << std::endl;

    // GMP 側の乱数ステートの初期化とシード設定
    gmp_randstate_t gmp_state;
    gmp_randinit_default(gmp_state);
    gmp_randseed_ui(gmp_state, seed);

    const std::size_t TEST_MAX_DIGITS = 300;
    PFI2 pfi_a(TEST_MAX_DIGITS);
    PFI2 pfi_b(TEST_MAX_DIGITS);

    // GMP 側の作業用多倍長整数の初期化（C ABI のため必須）
    mpz_t gmp_a, gmp_b;
    mpz_init(gmp_a);
    mpz_init(gmp_b);

    // 0: Aを新規ランダム生成, 1: Bを新規ランダム生成, 2: 符号反転操作, 3: 比較検証
    std::uniform_int_distribution<int> op_dist(0, 3);
    std::uniform_int_distribution<unsigned long> bit_dist(1, 1500); // 最大1500bit（約450桁）の巨大整数までカバー
    std::uniform_int_distribution<int> bool_dist(0, 1);

    for (std::size_t i = 0; i < iterations; ++i) {
        int op = op_dist(eng);

        if (op == 0) {
            // --- 巨大なランダム整数 A を GMP 側で生成し、PFI2 へ同期 ---
            unsigned long bits = bit_dist(eng);
            mpz_urandomb(gmp_a, gmp_state, bits); // 0 から 2^bits - 1 までの乱数
            
            // 20% の確率で完全な 0 に落として「0付近の境界条件」をいじめる
            if (eng() % 5 == 0) {
                mpz_set_ui(gmp_a, 0);
            }
            // 50% の確率で負の数にする
            if (bool_dist(eng) == 1 && mpz_sgn(gmp_a) != 0) {
                mpz_neg(gmp_a, gmp_a);
            }

            // PFI2 側に流し込む。物理容量（300桁=2400bit）を超えない限り必ず成功する契約
            [[maybe_unused]] bool ok = pfi_a.from_mpz(gmp_a);
            assert(ok);
        }
        else if (op == 1) {
            // --- 巨大なランダム整数 B を GMP 側で生成し、PFI2 へ同期 ---
            unsigned long bits = bit_dist(eng);
            mpz_urandomb(gmp_b, gmp_state, bits);
            
            if (eng() % 5 == 0) {
                mpz_set_ui(gmp_b, 0);
            }
            if (bool_dist(eng) == 1 && mpz_sgn(gmp_b) != 0) {
                mpz_neg(gmp_b, gmp_b);
            }

            [[maybe_unused]] bool ok = pfi_b.from_mpz(gmp_b);
            assert(ok);
        }
        else if (op == 2) {
            // --- ピンポイントでの符号反転操作（0 収束時の不変条件も突く） ---
            if (mpz_sgn(gmp_a) != 0 && bool_dist(eng) == 1) {
                mpz_neg(gmp_a, gmp_a);
                [[maybe_unused]] bool ok = pfi_a.from_mpz(gmp_a);
                assert(ok);
            }
            if (mpz_sgn(gmp_b) != 0 && bool_dist(eng) == 1) {
                mpz_neg(gmp_b, gmp_b);
                [[maybe_unused]] bool ok = pfi_b.from_mpz(gmp_b);
                assert(ok);
            }
        }
        else if (op == 3) {
            // --- GMP vs PFI2 運命の比較検証フェーズ ---
            
            // 1. GMP 側が叩き出す絶対的正解（オラクル）の取得
            int gmp_res = mpz_cmp(gmp_a, gmp_b);
            // GMP の仕様に合わせ、正なら 1、負なら -1、等しければ 0 に正規化
            int expected_cmp = (gmp_res > 0) ? 1 : ((gmp_res < 0) ? -1 : 0);

            // 2. 自前の PFI2::cmp を実行
            int actual_cmp_ab = pfi_a.cmp(pfi_b);
            int actual_cmp_ba = pfi_b.cmp(pfi_a);

            // 3. 差分アサート（1ビットの不整合も許さない）
            assert(actual_cmp_ab == expected_cmp);
            assert(actual_cmp_ba == -expected_cmp); // 反対称性チェック

            // 4. 自分自身との比較の完全性
            assert(pfi_a.cmp(pfi_a) == 0);
            assert(pfi_b.cmp(pfi_b) == 0);

            // 5. 【相互変換の往復テスト】
            // PFI2 から逆エクスポートした mpz_t が、元の gmp_a/gmp_b と完全一致するか
            mpz_t check_a, check_b;
            mpz_init(check_a); mpz_init(check_b);
            
            pfi_a.to_mpz(check_a);
            pfi_b.to_mpz(check_b);

            assert(mpz_cmp(gmp_a, check_a) == 0);
            assert(mpz_cmp(gmp_b, check_b) == 0);

            mpz_clear(check_a); mpz_clear(check_b);
        }

        // 毎ステップ内部不変条件（active_words_ などの整合性）をチェック
        if (!pfi_a.invariant_holds() || !pfi_b.invariant_holds()) {
            std::cerr << "CRITICAL FAILURE: Invariant broken during GMP differential fuzzing at step " << i << std::endl;
            std::exit(1);
        }
    }

    // GMP リソースの安全な解放
    mpz_clear(gmp_a);
    mpz_clear(gmp_b);
    gmp_randclear(gmp_state);

    std::cout << "-> Passed GMP Differential cmp Test successfully!" << std::endl;
}


// --- ここまで検証済み(2026年6月6日18時24分) --- //


void run_gmp_differential_add_test(std::size_t iterations) {
    std::cout << "Running: GMP Differential add_abs_inplace Test (" << iterations << " iterations)..." << std::endl;

    // 乱数生成器の初期化
    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);
    std::cout << "   [GMP Add Fuzz Seed]: 0x" << std::hex << seed << std::dec << std::endl;

    // GMP 側の乱数ステートの初期化
    gmp_randstate_t gmp_state;
    gmp_randinit_default(gmp_state);
    gmp_randseed_ui(gmp_state, seed);

    // 最大物理容量を 300 ワード（5^8進数で 900桁 ≒ 約 16718ビット）に設定
    const std::size_t TEST_MAX_WORDS = 300;

    // 数理計算: 5^24 ≒ 5.96e16。1ワードあたりのビット数は log2(5^24) = 24 * log2(5) ≒ 55.726 ビット。
    // 物理容量に確実に収まる最大ビット数を安全側に丸めて（floor）計算
    // std::log2 は constexpr ではないため、初期化時に一度計算する
    const unsigned long BITS_PER_WORD = static_cast<unsigned long>(PFI2::DIGITS_PER_WORD * std::log2(PFI2::BASE) * 0.8); // ≒ 55.72626
    const unsigned long MAX_SAFE_BITS = TEST_MAX_WORDS * BITS_PER_WORD;

    // GMP 側の作業用変数の初期化
    mpz_t gmp_a, gmp_b, gmp_res;
    mpz_init(gmp_a);
    mpz_init(gmp_b);
    mpz_init(gmp_res);
    mpz_t pfi_gmp_res;
    mpz_init(pfi_gmp_res);
    // GMP変数の準備
    mpz_t base_pow_start, base_pow_end, lower_part, upper_part, fill_val;
    mpz_init(base_pow_start);
    mpz_init(base_pow_end);
    mpz_init(lower_part);
    mpz_init(upper_part);
    mpz_init(fill_val);
    mpz_t gmp_limit;
    mpz_init(gmp_limit);
    // 乱数分布の設定
    // 0 から 15000 ビット（約 0 〜 808 ワード）の範囲で、A と B のサイズを完全に独立して決定
    // これにより、lhs_active > rhs_active と rhs_active > lhs_active の両方を激しくいじめる
    std::uniform_int_distribution<unsigned long> bit_dist(1, MAX_SAFE_BITS);
    std::uniform_int_distribution<int> edge_case_dist(0, 9);

    for (std::size_t i = 0; i < iterations; ++i) {
        // テストごとにインスタンスを新規作成して内部の生ゴミ状態や遅延初期化ポテンシャルをリセット
        PFI2 pfi_a(TEST_MAX_WORDS * 3); // max_digits
        PFI2 pfi_b(TEST_MAX_WORDS * 3);

        unsigned long bits_a = bit_dist(eng);
        unsigned long bits_b = bit_dist(eng);

        mpz_urandomb(gmp_a, gmp_state, bits_a);
        mpz_urandomb(gmp_b, gmp_state, bits_b);

        // --- エッジケース注入のハック ---
        // 10% の確率で A または B を完全な 0 にして境界条件を突く
        if (edge_case_dist(eng) == 0) mpz_set_ui(gmp_a, 0);
        if (edge_case_dist(eng) == 1) mpz_set_ui(gmp_b, 0);

        // 10% の確率で、キャリーのドミノ倒し（SKIP_SIGNAL_WORD）を強制誘発させる
        // 10% の確率で、既存のランダムな gmp_a の一部を 5^8 進数で MAX_DIGIT の連続に書き換える
        if (edge_case_dist(eng) == 2) {
            // bits_a から、おおよその最大5^8進数桁数を逆算（1桁あたり約19.17ビットなので安全側に振る）
            std::size_t approx_max_digits = (bits_a / 19) + 1;
            
            // 書き換え開始桁インデックス（0オリジン）をランダムに決定
            std::size_t idx_start = (approx_max_digits > 0) ? (eng() % approx_max_digits) : 0;
            // 連続する桁数（3桁パックの境界を跨がせるために4桁〜5桁程度を狙う）
            std::size_t len = 5;



            // 境界の重みを計算
            mpz_ui_pow_ui(base_pow_start, PFI2::BASE, idx_start);       // BASE^idx_start
            mpz_ui_pow_ui(base_pow_end, PFI2::BASE, idx_start + len);   // BASE^(idx_start + len)

            // 1. 元の数の「下位桁（idx_start より下）」を抽出
            mpz_fdiv_r(lower_part, gmp_a, base_pow_start);

            // 2. 元の数の「上位桁（idx_start + len 以上）」を抽出
            mpz_fdiv_q(upper_part, gmp_a, base_pow_end);

            // 3. 埋め尽くすための MAX_DIGIT の壁の値を計算: (BASE^len - 1) * BASE^idx_start
            mpz_ui_pow_ui(fill_val, PFI2::BASE, len);
            mpz_sub_ui(fill_val, fill_val, 1);
            mpz_mul(fill_val, fill_val, base_pow_start);

            // 4. すべてを完全に再結合して gmp_a を上書き
            // gmp_a = (upper_part * BASE^(idx_start + len)) + fill_val + lower_part
            mpz_mul(gmp_a, upper_part, base_pow_end);
            mpz_add(gmp_a, gmp_a, fill_val);
            mpz_add(gmp_a, gmp_a, lower_part);


            
            // これで、元のランダムなビット列の性質を保ったまま、
            // 指定した 5^8 進数の桁だけが完全に 390624 に差し替わった極悪なテストケースが完成
        }

        // PFI2 側へデータを流し込む（絶対値テストなので符号は正にする）
        mpz_abs(gmp_a, gmp_a);
        mpz_abs(gmp_b, gmp_b);

        [[maybe_unused]] bool ok_a = pfi_a.from_mpz(gmp_a);
        [[maybe_unused]] bool ok_b = pfi_b.from_mpz(gmp_b);
        assert(ok_a && ok_b);

        // --- GMP 側の絶対値加算（正解オラクル） ---
        mpz_add(gmp_res, gmp_a, gmp_b);

        // --- PFI2 側の絶対値加算カーネル実行 ---
        // 物理容量の上限を超えない限り、必ず成功する設計
        bool ok_sum = pfi_a.add_abs_inplace(pfi_b);

        // ----------------------------------------------------------------
        // フェーズ 3: 最終キャリー処理とオーバーフロー防御の厳密なアサート
        // ----------------------------------------------------------------
        // PFI2 の最大表現可能限界値（Limit = BASE^(3 * TEST_MAX_WORDS)）を計算

        mpz_ui_pow_ui(gmp_limit, PFI2::BASE, 3 * TEST_MAX_WORDS);
        
        // gmp_res >= gmp_limit ならば、物理容量を数学的にオーバーフローしている
        bool expect_overflow = (mpz_cmp(gmp_res, gmp_limit) >= 0);

        // 1. カーネルの戻り値の成否と、数学的オーバーフロー予測の整合性を完璧に突き合わせる
        if (expect_overflow) {
            // 数学的に溢れるはずなら、ok_sum は絶対に false でなければならない
            if (ok_sum) {
                std::cerr << "CRITICAL FAILURE: add_abs_inplace returned true, but it should have overflowed mathematically at iteration " << i << std::endl;
                gmp_printf("   gmp_res = %Zd\n", gmp_res);
                std::exit(1);
            }
            // 正常に検知して false を返した場合は、このステップはスキップして次のテストへ
            continue; 
        } else {
            // 数学的に収まるはずなら、ok_sum は絶対に true でなければならない
            if (!ok_sum) {
                std::cerr << "CRITICAL FAILURE: add_abs_inplace returned false (overflow), but there is enough capacity at iteration " << i << std::endl;
                gmp_printf("   gmp_res = %Zd\n", gmp_res);
                std::exit(1);
            }
        }

        // --- 整合性チェック（往復検証） ---
        pfi_a.to_mpz(pfi_gmp_res);

        // 1ビットの狂いもなく完全一致するかアサート
        if (mpz_cmp(gmp_res, pfi_gmp_res) != 0) {
            std::cerr << "CRITICAL FAILURE: Mismatch in add_abs_inplace at iteration " << i << std::endl;
            gmp_printf("   gmp_a    = %Zd\n", gmp_a);
            gmp_printf("   gmp_b    = %Zd\n", gmp_b);
            gmp_printf("   Expected = %Zd\n", gmp_res);
            gmp_printf("   Actual   = %Zd\n", pfi_gmp_res);
            std::exit(1);
        }

        // 内部不変条件（active_words_ が w と完全同期しているか、最上位が0になっていないか）のチェック
        if (!pfi_a.invariant_holds()) {
            std::cerr << "CRITICAL FAILURE: Invariant broken after add_abs_inplace at iteration " << i << std::endl;
            std::exit(1);
        }

        
    }

    // GMP リソースの安全な解放
    mpz_clear(gmp_limit);
    mpz_clear(base_pow_start);
    mpz_clear(base_pow_end);
    mpz_clear(lower_part);
    mpz_clear(upper_part);
    mpz_clear(fill_val);
    mpz_clear(gmp_a);
    mpz_clear(gmp_b);
    mpz_clear(gmp_res);
    gmp_randclear(gmp_state);
    mpz_clear(pfi_gmp_res);

    std::cout << "-> Passed GMP Differential add_abs_inplace Test successfully!" << std::endl;
}


// --- ここまで検証済み2026年6月8日23時45分 ---


void run_gmp_differential_sub_test(std::size_t iterations) {
    std::cout << "Running: GMP Differential sub_abs_inplace Test (" << iterations << " iterations)..." << std::endl;

    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);
    std::cout << "   [GMP Sub Fuzz Seed]: 0x" << std::hex << seed << std::dec << std::endl;

    gmp_randstate_t gmp_state;
    gmp_randinit_default(gmp_state);
    gmp_randseed_ui(gmp_state, seed);

    const std::size_t TEST_MAX_WORDS = 300;
    const unsigned long BITS_PER_WORD = static_cast<unsigned long>(PFI2::DIGITS_PER_WORD * std::log2(PFI2::BASE) * 0.8);
    const unsigned long MAX_SAFE_BITS = TEST_MAX_WORDS * BITS_PER_WORD;

    mpz_t gmp_a, gmp_b, gmp_res, pfi_gmp_res;
    mpz_init(gmp_a);
    mpz_init(gmp_b);
    mpz_init(gmp_res);
    mpz_init(pfi_gmp_res);

    mpz_t base_pow_start, base_pow_end, lower_part, upper_part, low_noise;
    mpz_init(base_pow_start);
    mpz_init(base_pow_end);
    mpz_init(lower_part);
    mpz_init(upper_part);
    mpz_init(low_noise);

    // 独立した6つの生成戦略を均等にサンプリングする
    std::uniform_int_distribution<int> strategy_dist(0, 5);
    std::uniform_int_distribution<unsigned long> bit_dist(1, MAX_SAFE_BITS);

    for (std::size_t i = 0; i < iterations; ++i) {
        PFI2 pfi_a(TEST_MAX_WORDS * 3); 
        PFI2 pfi_b(TEST_MAX_WORDS * 3);

        unsigned long bits_a = bit_dist(eng);
        unsigned long bits_b = bit_dist(eng);

        int strategy = strategy_dist(eng);

        switch (strategy) {
            case 0: // 【戦略0】完全ランダムペア (A < B が約50%で発生)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 1: // 【戦略1】ビット長を完全一致させ、データレベルの大小関係を競わせる (active_words_ 一致)
                bits_b = bits_a;
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 2: // 【戦略2】A と B を同一値にした上で、微小ノイズで大小を狂わせる (極限のニアピンケース)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set(gmp_b, gmp_a);
                mpz_set_ui(low_noise, eng() % 100000);
                if (eng() % 2 == 0) {
                    mpz_add(gmp_b, gmp_b, low_noise); // B の方がわずかに大きい
                } else {
                    mpz_sub(gmp_b, gmp_b, low_noise); // A の方がわずかに大きい
                }
                break;

            case 3: // 【戦略3】B が完全なる 0 の境界条件ケース
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set_ui(gmp_b, 0);
                break;

            case 4: // 【戦略4】A == B (結果が完全な 0 に潰れるゼロ・コラプス最大ケース)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set(gmp_b, gmp_a);
                break;

            case 5: // 【戦略5】A >= B を維持しつつ、A の内部に「0 の山」を仕込んで SKIP_SIGNAL ドミノを殺しにいく
                if (bits_a < bits_b) std::swap(bits_a, bits_b);
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                if (mpz_cmp(gmp_a, gmp_b) < 0) mpz_swap(gmp_a, gmp_b);
                
                // A の中間に 0 の連続を埋め込むハック
                {
                    std::size_t approx_max_digits = (bits_a / 19) + 1;
                    std::size_t idx_start = (approx_max_digits > 0) ? (eng() % approx_max_digits) : 0;
                    std::size_t len = 5;
                    mpz_ui_pow_ui(base_pow_start, PFI2::BASE, idx_start);
                    mpz_ui_pow_ui(base_pow_end, PFI2::BASE, idx_start + len);
                    mpz_fdiv_r(lower_part, gmp_a, base_pow_start);
                    mpz_fdiv_q(upper_part, gmp_a, base_pow_end);
                    mpz_mul(gmp_a, upper_part, base_pow_end);
                    mpz_add(gmp_a, gmp_a, lower_part);
                }
                // 再び安全弁
                if (mpz_cmp(gmp_a, gmp_b) < 0) mpz_swap(gmp_a, gmp_b);
                break;
        }

        // 絶対値のテストにするため、符号は確実に正に倒す
        mpz_abs(gmp_a, gmp_a);
        mpz_abs(gmp_b, gmp_b);

        // PFI2 に充填
        [[maybe_unused]] bool ok_a = pfi_a.from_mpz(gmp_a);
        [[maybe_unused]] bool ok_b = pfi_b.from_mpz(gmp_b);
        assert(ok_a && ok_b);

        // 数学的な正解予測（オラクル）
        bool expect_success = (mpz_cmp(gmp_a, gmp_b) >= 0);

        // カーネル実行
        bool ok_sub = pfi_a.sub_abs_inplace(pfi_b);

        // 1. 戻り値の真偽値チェック
        if (ok_sub != expect_success) {
            std::cerr << "CRITICAL FAILURE: sub_abs_inplace returned " << (ok_sub ? "true" : "false")
                      << ", but expected " << (expect_success ? "true" : "false") << " at iteration " << i << std::endl;
            gmp_printf("   gmp_a = %Zd\n", gmp_a);
            gmp_printf("   gmp_b = %Zd\n", gmp_b);
            std::exit(1);
        }

        if (!ok_sub) {
            // ----------------------------------------------------------------
            // 2. 失敗（false）した時の反証アサート
            // ----------------------------------------------------------------
            // 戻り値が false である以上、数学的に「確実に A < B であったこと」を GMP 側で厳密に検証
            if (mpz_cmp(gmp_a, gmp_b) >= 0) {   // もし A >= B ならエラー
                std::cerr << "CRITICAL FAILURE: sub_abs_inplace returned false, but mathematically A >= B at iteration " << i << std::endl;
                gmp_printf("   gmp_a = %Zd\n", gmp_a);
                gmp_printf("   gmp_b = %Zd\n", gmp_b);
                std::exit(1);
            }

            // 【引数 rhs (pfi_b) の不変性チェック】
            // 呼び出し側の契約として、引数に渡した rhs が巻き添えで破壊されることは許されない。
            // 演算に失敗した場合でも、pfi_b が完全に無傷であることを保証する。
            pfi_b.to_mpz(pfi_gmp_res);
            if (mpz_cmp(gmp_b, pfi_gmp_res) != 0) {
                std::cerr << "CRITICAL FAILURE: sub_abs_inplace failed, but MUTATED the argument rhs (pfi_b) at iteration " << i << std::endl;
                gmp_printf("   Original B = %Zd\n", gmp_b);
                gmp_printf("   Mutated B  = %Zd\n", pfi_gmp_res);
                std::exit(1);
            }

            // 【修正版：this の安全弁チェック】
            // 内部データ構造にアクセスして、ポインタのバーストやactive_words_の物理破綻がないかだけを確認。
            // `invariant_holds()` の前半部分と同等の防衛線をマニュアルで敷く。
            // (※もし、クラスのメンバ変数 `active_words_` や `capacity_words_` が private で
            //  テスト関数から直接触れない場合は、これらを公開する軽量な getter を仕込むか、
            //  失敗時専用の緩い検証関数 `bool is_physically_safe() const` を本体側に1つ用意するのがスマート)
            
            /* もしメンバに直接アクセスできる、あるいは軽量 getter がある場合のコード */
            if (pfi_a.active_words() > TEST_MAX_WORDS * 3) {
                std::cerr << "CRITICAL FAILURE: sub_abs_inplace failed, and active_words_ blew up dangerously!" << std::endl;
                std::exit(1);
            }

            continue; // 次のランダムテストへ
        } else {
            // 3. 成功（true）した時、結果が完全一致することを確認
            mpz_sub(gmp_res, gmp_a, gmp_b);
            pfi_a.to_mpz(pfi_gmp_res);

            if (mpz_cmp(gmp_res, pfi_gmp_res) != 0) {
                std::cerr << "CRITICAL FAILURE: Mismatch in sub_abs_inplace result at iteration " << i << std::endl;
                gmp_printf("   gmp_a    = %Zd\n", gmp_a);
                gmp_printf("   gmp_b    = %Zd\n", gmp_b);
                gmp_printf("   Expected = %Zd\n", gmp_res);
                gmp_printf("   Actual   = %Zd\n", pfi_gmp_res);
                std::exit(1);
            }
        }

        // 不変条件アサート
        if (!pfi_a.invariant_holds()) {
            std::cerr << "CRITICAL FAILURE: Invariant broken after sub_abs_inplace at iteration " << i << std::endl;
            std::exit(1);
        }
    }

    mpz_clear(low_noise); mpz_clear(base_pow_start); mpz_clear(base_pow_end);
    mpz_clear(lower_part); mpz_clear(upper_part);
    mpz_clear(gmp_a); mpz_clear(gmp_b); mpz_clear(gmp_res); pfi_gmp_res;
    gmp_randclear(gmp_state);
    mpz_clear(pfi_gmp_res);

    std::cout << "-> Passed GMP Differential sub_abs_inplace Test successfully!" << std::endl;
}


// --- 2026年6月9日16時39分 ---

void run_gmp_differential_sub_from_test(std::size_t iterations) {
    std::cout << "Running: GMP Differential sub_abs_from_inplace Test (" << iterations << " iterations)..." << std::endl;

    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);
    std::cout << "   [GMP Sub From Fuzz Seed]: 0x" << std::hex << seed << std::dec << std::endl;

    gmp_randstate_t gmp_state;
    gmp_randinit_default(gmp_state);
    gmp_randseed_ui(gmp_state, seed);

    const std::size_t TEST_MAX_WORDS = 300;
    const unsigned long BITS_PER_WORD = static_cast<unsigned long>(PFI2::DIGITS_PER_WORD * std::log2(PFI2::BASE) * 0.8);
    const unsigned long MAX_SAFE_BITS = TEST_MAX_WORDS * BITS_PER_WORD;

    mpz_t gmp_a, gmp_b, gmp_res, pfi_gmp_res;
    mpz_init(gmp_a);
    mpz_init(gmp_b);
    mpz_init(gmp_res);
    mpz_init(pfi_gmp_res);

    mpz_t base_pow_start, base_pow_end, lower_part, upper_part, low_noise;
    mpz_init(base_pow_start);
    mpz_init(base_pow_end);
    mpz_init(lower_part);
    mpz_init(upper_part);
    mpz_init(low_noise);

    // 独立した6つの生成戦略を均等にサンプリングする
    std::uniform_int_distribution<int> strategy_dist(0, 5);
    std::uniform_int_distribution<unsigned long> bit_dist(1, MAX_SAFE_BITS);

    for (std::size_t i = 0; i < iterations; ++i) {
        // 固定容量、コピー禁止寄りの設計。キャパシティは十分に確保
        PFI2 pfi_a(TEST_MAX_WORDS * 3); 
        PFI2 pfi_b(TEST_MAX_WORDS * 3);

        unsigned long bits_a = bit_dist(eng);
        unsigned long bits_b = bit_dist(eng);

        int strategy = strategy_dist(eng);

        switch (strategy) {
            case 0: // 【戦略0】完全ランダムペア (A <= B が約50%で発生)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 1: // 【戦略1】ビット長を完全一致させ、データレベルの大小関係を競わせる (active_words_ 一致)
                bits_b = bits_a;
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 2: // 【戦略2】A と B を同一値にした上で、微小ノイズで大小を狂わせる (極限のニアピンケース)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set(gmp_b, gmp_a);
                mpz_set_ui(low_noise, eng() % 100000);
                if (eng() % 2 == 0) {
                    mpz_add(gmp_b, gmp_b, low_noise); // B の方がわずかに大きい (成功パターン)
                } else {
                    mpz_sub(gmp_b, gmp_b, low_noise); // A の方がわずかに大きい (失敗パターン)
                }
                break;

            case 3: // 【戦略3】A が完全なる 0 の境界条件ケース (確実に成功し B がそのままコピーされる)
                mpz_set_ui(gmp_a, 0);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 4: // 【戦略4】A == B (結果が完全な 0 に潰れるゼロ・コラプス最大ケース)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set(gmp_b, gmp_a);
                break;

            case 5: // 【戦略5】B >= A を維持しつつ、B の内部に「0 の山」を仕込んで SKIP_SIGNAL ドミノを殺しにいく
                if (bits_a > bits_b) std::swap(bits_a, bits_b);
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                if (mpz_cmp(gmp_b, gmp_a) < 0) mpz_swap(gmp_a, gmp_b);
                
                // B の中間に 0 の連続を埋め込むハック
                {
                    std::size_t approx_max_digits = (bits_b / 19) + 1;
                    std::size_t idx_start = (approx_max_digits > 0) ? (eng() % approx_max_digits) : 0;
                    std::size_t len = 5;
                    mpz_ui_pow_ui(base_pow_start, PFI2::BASE, idx_start);
                    mpz_ui_pow_ui(base_pow_end, PFI2::BASE, idx_start + len);
                    mpz_fdiv_r(lower_part, gmp_b, base_pow_start);
                    mpz_fdiv_q(upper_part, gmp_b, base_pow_end);
                    mpz_mul(gmp_b, upper_part, base_pow_end);
                    mpz_add(gmp_b, gmp_b, lower_part);
                }
                // 再び安全弁
                if (mpz_cmp(gmp_b, gmp_a) < 0) mpz_swap(gmp_a, gmp_b);
                break;
        }

        // 絶対値カーネルのテストのため符号は確実に正に倒す
        mpz_abs(gmp_a, gmp_a);
        mpz_abs(gmp_b, gmp_b);

        // PFI2 にデータを充填
        [[maybe_unused]] bool ok_a = pfi_a.from_mpz(gmp_a);
        [[maybe_unused]] bool ok_b = pfi_b.from_mpz(gmp_b);
        assert(ok_a && ok_b);

        // 数学的な正解予測（オラクル）： sub_abs_from_inplace は |rhs| >= |this|、つまり B >= A で成功する
        bool expect_success = (mpz_cmp(gmp_b, gmp_a) >= 0);

        // カーネル実行： pfi_a <- |pfi_b| - |pfi_a|
        bool ok_sub = pfi_a.sub_abs_from_inplace(pfi_b);

        // 1. 戻り値の真偽値チェック
        if (ok_sub != expect_success) {
            std::cerr << "CRITICAL FAILURE: sub_abs_from_inplace returned " << (ok_sub ? "true" : "false")
                      << ", but expected " << (expect_success ? "true" : "false") << " at iteration " << i << std::endl;
            gmp_printf("   gmp_a (this) = %Zd\n", gmp_a);
            gmp_printf("   gmp_b (rhs)  = %Zd\n", gmp_b);
            std::exit(1);
        }

        if (!ok_sub) {
            // ----------------------------------------------------------------
            // 2. 失敗（false）した時の検証
            // ----------------------------------------------------------------
            // 戻り値が false である以上、数学的に「確実に B < A であったこと」を検証
            if (mpz_cmp(gmp_b, gmp_a) >= 0) {
                std::cerr << "CRITICAL FAILURE: sub_abs_from_inplace returned false, but mathematically B >= A at iteration " << i << std::endl;
                gmp_printf("   gmp_a = %Zd\n", gmp_a);
                gmp_printf("   gmp_b = %Zd\n", gmp_b);
                std::exit(1);
            }

            // 【引数 rhs (pfi_b) の不変性チェック】
            // 演算成否に関わらず、引数 rhs のバッファが破壊されてはならない
            pfi_b.to_mpz(pfi_gmp_res);
            if (mpz_cmp(gmp_b, pfi_gmp_res) != 0) {
                std::cerr << "CRITICAL FAILURE: sub_abs_from_inplace failed, but MUTATED the argument rhs (pfi_b) at iteration " << i << std::endl;
                gmp_printf("   Original B = %Zd\n", gmp_b);
                gmp_printf("   Mutated B  = %Zd\n", pfi_gmp_res);
                std::exit(1);
            }

            // 【this の物理安全弁チェック】
            // 仕様上「this の内容は保持されない」が、メモリ空間を突き破る破綻が起きていないかチェック
            // getter 経由、もしくはメンバアクセス可能という前提
            if (pfi_a.active_words() > TEST_MAX_WORDS * 3) {
                std::cerr << "CRITICAL FAILURE: sub_abs_from_inplace failed, and active_words_ blew up dangerously!" << std::endl;
                std::exit(1);
            }

            continue; 
        } else {
            // ---------------------------------------------------------
            // 3. 成功（true）した時、結果が完全一致することを確認
            // ---------------------------------------------------------
            // 結果は B - A
            mpz_sub(gmp_res, gmp_b, gmp_a);
            pfi_a.to_mpz(pfi_gmp_res);

            if (mpz_cmp(gmp_res, pfi_gmp_res) != 0) {
                std::cerr << "CRITICAL FAILURE: Mismatch in sub_abs_from_inplace result at iteration " << i << std::endl;
                gmp_printf("   gmp_a (this) = %Zd\n", gmp_a);
                gmp_printf("   gmp_b (rhs)  = %Zd\n", gmp_b);
                gmp_printf("   Expected     = %Zd\n", gmp_res);
                gmp_printf("   Actual       = %Zd\n", pfi_gmp_res);
                std::exit(1);
            }
        }

        // 成功時のみ不変条件の厳密アサートをチェック（符号は維持、active_words_ のトリムが正常か）
        if (!pfi_a.invariant_holds()) {
            std::cerr << "CRITICAL FAILURE: Invariant broken after sub_abs_from_inplace at iteration " << i << std::endl;
            std::exit(1);
        }
    }

    mpz_clear(low_noise); mpz_clear(base_pow_start); mpz_clear(base_pow_end);
    mpz_clear(lower_part); mpz_clear(upper_part);
    mpz_clear(gmp_a); mpz_clear(gmp_b); mpz_clear(gmp_res); mpz_clear(pfi_gmp_res);
    gmp_randclear(gmp_state);

    std::cout << "-> Passed GMP Differential sub_abs_from_inplace Test successfully!" << std::endl;
}

// --- 2026年6月28日15時10分 ---

void run_self_alias_test(std::size_t iterations) {
    std::cout
        << "Running: Self Alias Test ("
        << iterations
        << " iterations)..."
        << std::endl;

    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);

    std::cout
        << "   [Alias Seed]: 0x"
        << std::hex << seed
        << std::dec
        << std::endl;

    gmp_randstate_t gmp_state;
    gmp_randinit_default(gmp_state);
    gmp_randseed_ui(gmp_state, seed);

    constexpr std::size_t TEST_WORDS = 300;

    mpz_t gmp_original;
    mpz_t gmp_expected;
    mpz_t gmp_actual;

    mpz_init(gmp_original);
    mpz_init(gmp_expected);
    mpz_init(gmp_actual);

    const unsigned long MAX_BITS =
        static_cast<unsigned long>(
            TEST_WORDS *
            PFI2::DIGITS_PER_WORD *
            std::log2(PFI2::BASE) * 0.8
        );

    std::uniform_int_distribution<unsigned long>
        bit_dist(0, MAX_BITS);

    for (std::size_t i = 0; i < iterations; ++i) {

        unsigned long bits = bit_dist(eng);

        mpz_urandomb(gmp_original, gmp_state, bits);

        //------------------------------------------------------------------
        // add_abs_inplace(self)
        //------------------------------------------------------------------
        {
            PFI2 a(TEST_WORDS * 3);

            bool ok = a.from_mpz(gmp_original);
            assert(ok);

            mpz_mul_ui(gmp_expected, gmp_original, 2);

            bool r = a.add_abs_inplace(a);

            if (!r) {
                std::cerr
                    << "add_abs_inplace(self) returned false at "
                    << i << std::endl;
                std::exit(1);
            }

            a.to_mpz(gmp_actual);

            if (mpz_cmp(gmp_expected, gmp_actual) != 0) {
                std::cerr
                    << "FAIL add_abs_inplace(self) at "
                    << i << std::endl;

                gmp_printf("orig     = %Zd\n", gmp_original);
                gmp_printf("expected = %Zd\n", gmp_expected);
                gmp_printf("actual   = %Zd\n", gmp_actual);

                std::exit(1);
            }

            assert(a.invariant_holds());
        }

        //------------------------------------------------------------------
        // sub_abs_inplace(self)
        //------------------------------------------------------------------
        {
            PFI2 a(TEST_WORDS * 3);

            bool ok = a.from_mpz(gmp_original);
            assert(ok);

            mpz_set_ui(gmp_expected, 0);

            bool r = a.sub_abs_inplace(a);

            if (!r) {
                std::cerr
                    << "sub_abs_inplace(self) returned false at "
                    << i << std::endl;
                std::exit(1);
            }

            a.to_mpz(gmp_actual);

            if (mpz_cmp(gmp_expected, gmp_actual) != 0) {
                std::cerr
                    << "FAIL sub_abs_inplace(self) at "
                    << i << std::endl;

                gmp_printf("orig     = %Zd\n", gmp_original);
                gmp_printf("expected = %Zd\n", gmp_expected);
                gmp_printf("actual   = %Zd\n", gmp_actual);

                std::exit(1);
            }

            assert(a.invariant_holds());
        }

        //------------------------------------------------------------------
        // sub_abs_from_inplace(self)
        //------------------------------------------------------------------
        {
            PFI2 a(TEST_WORDS * 3);

            bool ok = a.from_mpz(gmp_original);
            assert(ok);

            mpz_set_ui(gmp_expected, 0);

            bool r = a.sub_abs_from_inplace(a);

            if (!r) {
                std::cerr
                    << "sub_abs_from_inplace(self) returned false at "
                    << i << std::endl;
                std::exit(1);
            }

            a.to_mpz(gmp_actual);

            if (mpz_cmp(gmp_expected, gmp_actual) != 0) {
                std::cerr
                    << "FAIL sub_abs_from_inplace(self) at "
                    << i << std::endl;

                gmp_printf("orig     = %Zd\n", gmp_original);
                gmp_printf("expected = %Zd\n", gmp_expected);
                gmp_printf("actual   = %Zd\n", gmp_actual);

                std::exit(1);
            }

            assert(a.invariant_holds());
        }

        //------------------------------------------------------------------
        // add_inplace(self)
        //------------------------------------------------------------------
        {
            PFI2 a(TEST_WORDS * 3);

            bool ok = a.from_mpz(gmp_original);
            assert(ok);

            mpz_mul_ui(gmp_expected, gmp_original, 2);

            bool r = a.add_inplace(a);

            if (!r) {
                std::cerr
                    << "add_inplace(self) returned false at "
                    << i << std::endl;
                std::exit(1);
            }

            a.to_mpz(gmp_actual);

            if (mpz_cmp(gmp_expected, gmp_actual) != 0) {
                std::cerr
                    << "FAIL add_inplace(self) at "
                    << i << std::endl;

                gmp_printf("orig     = %Zd\n", gmp_original);
                gmp_printf("expected = %Zd\n", gmp_expected);
                gmp_printf("actual   = %Zd\n", gmp_actual);

                std::exit(1);
            }

            assert(a.invariant_holds());
        }

        //------------------------------------------------------------------
        // sub_inplace(self)
        //------------------------------------------------------------------
        {
            PFI2 a(TEST_WORDS * 3);

            bool ok = a.from_mpz(gmp_original);
            assert(ok);

            mpz_set_ui(gmp_expected, 0);

            bool r = a.sub_inplace(a);

            if (!r) {
                std::cerr
                    << "sub_inplace(self) returned false at "
                    << i << std::endl;
                std::exit(1);
            }

            a.to_mpz(gmp_actual);

            if (mpz_cmp(gmp_expected, gmp_actual) != 0) {
                std::cerr
                    << "FAIL sub_inplace(self) at "
                    << i << std::endl;

                gmp_printf("orig     = %Zd\n", gmp_original);
                gmp_printf("expected = %Zd\n", gmp_expected);
                gmp_printf("actual   = %Zd\n", gmp_actual);

                std::exit(1);
            }

            assert(a.invariant_holds());
        }
    }

    mpz_clear(gmp_original);
    mpz_clear(gmp_expected);
    mpz_clear(gmp_actual);

    gmp_randclear(gmp_state);

    std::cout
        << "-> Passed Self Alias Test successfully!"
        << std::endl;
}

// --- 2026年6月28日15時15分 ---

void run_gmp_differential_add_inplace_test(std::size_t iterations) {
    std::cout << "Running: GMP Differential add_inplace Test (" << iterations << " iterations)..." << std::endl;

    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);
    std::cout << "   [GMP Add Inplace Fuzz Seed]: 0x" << std::hex << seed << std::dec << std::endl;

    gmp_randstate_t gmp_state;
    gmp_randinit_default(gmp_state);
    gmp_randseed_ui(gmp_state, seed);

    const std::size_t TEST_MAX_WORDS = 300;
    const unsigned long BITS_PER_WORD = static_cast<unsigned long>(PFI2::DIGITS_PER_WORD * std::log2(PFI2::BASE) * 0.8);
    const unsigned long MAX_SAFE_BITS = TEST_MAX_WORDS * BITS_PER_WORD;

    mpz_t gmp_a, gmp_b, gmp_res, pfi_gmp_res;
    mpz_init(gmp_a);
    mpz_init(gmp_b);
    mpz_init(gmp_res);
    mpz_init(pfi_gmp_res);

    // 符号反転やニアピンを制御するための分布
    std::uniform_int_distribution<int> strategy_dist(0, 5);
    std::uniform_int_distribution<unsigned long> bit_dist(1, MAX_SAFE_BITS);
    std::uniform_int_distribution<int> sign_dist(0, 3); // 符号の組み合わせ (++, +-, -+, --)

    for (std::size_t i = 0; i < iterations; ++i) {
        // キャパシティは十分に確保 (固定容量・コピー禁止制約下での検証)
        PFI2 pfi_a(TEST_MAX_WORDS * 3); 
        PFI2 pfi_b(TEST_MAX_WORDS * 3);

        unsigned long bits_a = bit_dist(eng);
        unsigned long bits_b = bit_dist(eng);

        int strategy = strategy_dist(eng);

        switch (strategy) {
            case 0: // 【戦略0】完全ランダムペア
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 1: // 【戦略1】絶対値の有効ワード数を一致させて微細境界を競わせる
                bits_b = bits_a;
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 2: // 【戦略2】絶対値をニアピンにし、異符号時に主客転倒とゼロ・コラプスを誘発
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set(gmp_b, gmp_a);
                if (eng() % 2 == 0) {
                    mpz_add_ui(gmp_b, gmp_b, eng() % 100); // |B| の方がわずかに大きい
                } else {
                    mpz_sub_ui(gmp_b, gmp_b, eng() % 100); // |A| の方がわずかに大きい
                }
                break;

            case 3: // 【戦略3】A が完全なる 0 の境界条件
                mpz_set_ui(gmp_a, 0);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 4: // 【戦略4】B が完全なる 0 の境界条件
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set_ui(gmp_b, 0);
                break;

            case 5: // 【戦略5】絶対値を完全に一致させる (A == -B の時に完全相殺)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set(gmp_b, gmp_a);
                break;
        }

        // ---------------------------------------------------------
        // 符号のインジェクション (++, +-, -+, --)
        // ---------------------------------------------------------
        int sign_pattern = sign_dist(eng);
        if (sign_pattern == 1) {        // (+ , -)
            mpz_neg(gmp_b, gmp_b);
        } else if (sign_pattern == 2) { // (- , +)
            mpz_neg(gmp_a, gmp_a);
        } else if (sign_pattern == 3) { // (- , -)
            mpz_neg(gmp_a, gmp_a);
            mpz_neg(gmp_b, gmp_b);
        }

        // PFI2 にデータを充填 (符号も内部で正しくパースされる想定)
        [[maybe_unused]] bool ok_a = pfi_a.from_mpz(gmp_a);
        [[maybe_unused]] bool ok_b = pfi_b.from_mpz(gmp_b);
        assert(ok_a && ok_b);

        // 物理キャパシティに基づく成否予測 (十分確保しているが契約検証として配置)
        // 本来は結果の active_words_ が収まるかだが、安全サイドとして rhs が収まるかで判定
        bool expect_success = (pfi_a.capacity_words() >= pfi_b.active_words());

        // オラクル演算 (GMP による符号付き加算)
        mpz_add(gmp_res, gmp_a, gmp_b);

        // ターゲット演算実行
        bool ok_add = pfi_a.add_inplace(pfi_b);

        // 1. 戻り値の契約チェック
        if (ok_add != expect_success) {
            std::cerr << "CRITICAL FAILURE: add_inplace returned " << (ok_add ? "true" : "false")
                      << ", but expected " << (expect_success ? "true" : "false") << " at iteration " << i << std::endl;
            gmp_printf("   gmp_a = %Zd\n", gmp_a);
            gmp_printf("   gmp_b = %Zd\n", gmp_b);
            std::exit(1);
        }

        if (!ok_add) {
            // 失敗時（容量不足など）、引数が巻き添えで破壊されていないかチェック
            pfi_b.to_mpz(pfi_gmp_res);
            if (mpz_cmp(gmp_b, pfi_gmp_res) != 0) {
                std::cerr << "CRITICAL FAILURE: add_inplace failed, but MUTATED rhs at iteration " << i << std::endl;
                std::exit(1);
            }
            continue;
        } else {
            // 2. 成功時、結果の代数的数値が完全一致するか
            pfi_a.to_mpz(pfi_gmp_res);

            if (mpz_cmp(gmp_res, pfi_gmp_res) != 0) {
                std::cerr << "CRITICAL FAILURE: Mismatch in add_inplace result at iteration " << i << std::endl;
                gmp_printf("   gmp_a    = %Zd\n", gmp_a);
                gmp_printf("   gmp_b    = %Zd\n", gmp_b);
                gmp_printf("   Expected = %Zd\n", gmp_res);
                gmp_printf("   Actual   = %Zd\n", pfi_gmp_res);
                std::exit(1);
            }
        }

        // 3. クラスの代数的・物理的不変条件が維持されているかチェック
        // (例: 結果が 0 のとき、符号が必ず false (正) にコラプスしているか等)
        if (!pfi_a.invariant_holds()) {
            std::cerr << "CRITICAL FAILURE: Invariant broken after add_inplace at iteration " << i << std::endl;
            gmp_printf("   gmp_a    = %Zd\n", gmp_a);
            gmp_printf("   gmp_b    = %Zd\n", gmp_b);
            gmp_printf("   Actual   = %Zd\n", pfi_gmp_res);
            std::exit(1);
        }
    }

    mpz_clear(gmp_a); mpz_clear(gmp_b); mpz_clear(gmp_res); mpz_clear(pfi_gmp_res);
    gmp_randclear(gmp_state);

    std::cout << "-> Passed GMP Differential add_inplace Test successfully!" << std::endl;
}

// --- 2026年6月28日15時19分 ---

void run_gmp_differential_sub_inplace_test(std::size_t iterations) {
    std::cout << "Running: GMP Differential sub_inplace Test (" << iterations << " iterations)..." << std::endl;

    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);
    std::cout << "   [GMP Sub Inplace Fuzz Seed]: 0x" << std::hex << seed << std::dec << std::endl;

    gmp_randstate_t gmp_state;
    gmp_randinit_default(gmp_state);
    gmp_randseed_ui(gmp_state, seed);

    const std::size_t TEST_MAX_WORDS = 300;
    const unsigned long BITS_PER_WORD = static_cast<unsigned long>(PFI2::DIGITS_PER_WORD * std::log2(PFI2::BASE) * 0.8);
    const unsigned long MAX_SAFE_BITS = TEST_MAX_WORDS * BITS_PER_WORD;

    mpz_t gmp_a, gmp_b, gmp_res, pfi_gmp_res;
    mpz_init(gmp_a);
    mpz_init(gmp_b);
    mpz_init(gmp_res);
    mpz_init(pfi_gmp_res);

    std::uniform_int_distribution<int> strategy_dist(0, 5);
    std::uniform_int_distribution<unsigned long> bit_dist(1, MAX_SAFE_BITS);
    std::uniform_int_distribution<int> sign_dist(0, 3); // (++, +-, -+, --)

    for (std::size_t i = 0; i < iterations; ++i) {
        PFI2 pfi_a(TEST_MAX_WORDS * 3); 
        PFI2 pfi_b(TEST_MAX_WORDS * 3);

        unsigned long bits_a = bit_dist(eng);
        unsigned long bits_b = bit_dist(eng);

        int strategy = strategy_dist(eng);

        switch (strategy) {
            case 0: // 【戦略0】完全ランダムペア
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 1: // 【戦略1】絶対値の桁（ワード数）を一致させて境界を競わせる
                bits_b = bits_a;
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 2: // 【戦略2】絶対値をニアピンにし、同符号時の主客転倒（符号トグル）を最大誘発
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set(gmp_b, gmp_a);
                if (eng() % 2 == 0) {
                    mpz_add_ui(gmp_b, gmp_b, eng() % 100); // |B| の方がわずかに大きい
                } else {
                    mpz_sub_ui(gmp_b, gmp_b, eng() % 100); // |A| の方がわずかに大きい
                }
                break;

            case 3: // 【戦略3】A が完全なる 0 の境界条件 (0 - B = -B となる符号反転ルート)
                mpz_set_ui(gmp_a, 0);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                break;

            case 4: // 【戦略4】B が完全なる 0 の境界条件 (A - 0 = A で無風通過ルート)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set_ui(gmp_b, 0);
                break;

            case 5: // 【戦略5】絶対値を完全一致させる (同符号なら A == B で 0 コラプスが発生)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_set(gmp_b, gmp_a);
                break;
        }

        // ---------------------------------------------------------
        // 符号のインジェクション (++, +-, -+, --)
        // ---------------------------------------------------------
        int sign_pattern = sign_dist(eng);
        if (sign_pattern == 1) {        // (+ , -) -> 実質絶対値加算
            mpz_neg(gmp_b, gmp_b);
        } else if (sign_pattern == 2) { // (- , +) -> 実質絶対値加算
            mpz_neg(gmp_a, gmp_a);
        } else if (sign_pattern == 3) { // (- , -) -> 実質絶対値減算
            mpz_neg(gmp_a, gmp_a);
            mpz_neg(gmp_b, gmp_b);
        }

        // PFI2 に充填
        [[maybe_unused]] bool ok_a = pfi_a.from_mpz(gmp_a);
        [[maybe_unused]] bool ok_b = pfi_b.from_mpz(gmp_b);
        assert(ok_a && ok_b);

        // 物理キャパシティに基づく成否予測
        bool expect_success = (pfi_a.capacity_words() >= pfi_b.active_words());

        // オラクル演算 (GMP による符号付き減算)
        mpz_sub(gmp_res, gmp_a, gmp_b);

        // ターゲット演算実行
        bool ok_sub = pfi_a.sub_inplace(pfi_b);

        // 1. 戻り値の契約チェック
        if (ok_sub != expect_success) {
            std::cerr << "CRITICAL FAILURE: sub_inplace returned " << (ok_sub ? "true" : "false")
                      << ", but expected " << (expect_success ? "true" : "false") << " at iteration " << i << std::endl;
            gmp_printf("   gmp_a = %Zd\n", gmp_a);
            gmp_printf("   gmp_b = %Zd\n", gmp_b);
            std::exit(1);
        }

        if (!ok_sub) {
            // 失敗時、引数の不変性チェック
            pfi_b.to_mpz(pfi_gmp_res);
            if (mpz_cmp(gmp_b, pfi_gmp_res) != 0) {
                std::cerr << "CRITICAL FAILURE: sub_inplace failed, but MUTATED rhs at iteration " << i << std::endl;
                std::exit(1);
            }
            continue;
        } else {
            // 2. 成功時、数値の完全一致検証
            pfi_a.to_mpz(pfi_gmp_res);

            if (mpz_cmp(gmp_res, pfi_gmp_res) != 0) {
                std::cerr << "CRITICAL FAILURE: Mismatch in sub_inplace result at iteration " << i << std::endl;
                gmp_printf("   gmp_a    = %Zd\n", gmp_a);
                gmp_printf("   gmp_b    = %Zd\n", gmp_b);
                gmp_printf("   Expected = %Zd\n", gmp_res);
                gmp_printf("   Actual   = %Zd\n", pfi_gmp_res);
                std::exit(1);
            }
        }

        // 3. 代数的・物理的不変条件の維持チェック
        if (!pfi_a.invariant_holds()) {
            std::cerr << "CRITICAL FAILURE: Invariant broken after sub_inplace at iteration " << i << std::endl;
            gmp_printf("   gmp_a    = %Zd\n", gmp_a);
            gmp_printf("   gmp_b    = %Zd\n", gmp_b);
            gmp_printf("   Actual   = %Zd\n", pfi_gmp_res);
            std::exit(1);
        }
    }

    mpz_clear(gmp_a); mpz_clear(gmp_b); mpz_clear(gmp_res); mpz_clear(pfi_gmp_res);
    gmp_randclear(gmp_state);

    std::cout << "-> Passed GMP Differential sub_inplace Test successfully!" << std::endl;
}

// --- 2026年6月28日15時39分 ---

void run_gmp_differential_equal_test(std::size_t iterations) {
    std::cout << "Running: GMP Differential operator== Test (" << iterations << " iterations)..." << std::endl;

    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);
    std::cout << "   [GMP Equal Fuzz Seed]: 0x" << std::hex << seed << std::dec << std::endl;

    gmp_randstate_t gmp_state;
    gmp_randinit_default(gmp_state);
    gmp_randseed_ui(gmp_state, seed);

    const std::size_t TEST_MAX_WORDS = 300;
    const unsigned long BITS_PER_WORD = static_cast<unsigned long>(PFI2::DIGITS_PER_WORD * std::log2(PFI2::BASE) * 0.8);
    const unsigned long MAX_SAFE_BITS = TEST_MAX_WORDS * BITS_PER_WORD;

    mpz_t gmp_a, gmp_b;
    mpz_init(gmp_a);
    mpz_init(gmp_b);

    std::uniform_int_distribution<int> strategy_dist(0, 5);
    std::uniform_int_distribution<unsigned long> bit_dist(1, MAX_SAFE_BITS);
    std::uniform_int_distribution<int> sign_dist(0, 1);

    for (std::size_t i = 0; i < iterations; ++i) {
        PFI2 pfi_a(TEST_MAX_WORDS * 3);
        PFI2 pfi_b(TEST_MAX_WORDS * 3);

        unsigned long bits_a = bit_dist(eng);
        unsigned long bits_b = bit_dist(eng);

        int strategy = strategy_dist(eng);

        switch (strategy) {
            case 0: // 【戦略0】完全ランダムペア (ほぼ確実に false になるケース)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                mpz_urandomb(gmp_b, gmp_state, bits_b);
                // ランダムに符号をインジェクション
                if (sign_dist(eng)) mpz_neg(gmp_a, gmp_a);
                if (sign_dist(eng)) mpz_neg(gmp_b, gmp_b);
                break;

            case 1: // 【戦略1】完全に同一のビット長・同一のデータ (真に等値で memcmp まで完走するケース)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                if (sign_dist(eng)) mpz_neg(gmp_a, gmp_a);
                mpz_set(gmp_b, gmp_a);
                break;

            case 2: // 【戦略2】絶対値は完全に同一だが、符号だけを反転させる (符号不一致による早期脱出の検証)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                // A が 0 の場合は符号反転しても 0 のままなので、非ゼロを保証
                if (mpz_sgn(gmp_a) == 0) mpz_set_ui(gmp_a, 123456789ULL);
                
                mpz_set(gmp_b, gmp_a);
                mpz_neg(gmp_b, gmp_b); // B は A の符号反転
                break;

            case 3: // 【戦略3】両方とも完全なる 0 (ゼロ・コラプス同士の比較で active_words == 0 を通るケース)
                mpz_set_ui(gmp_a, 0);
                mpz_set_ui(gmp_b, 0);
                break;

            case 4: // 【戦略4】片方だけが完全なる 0 (active_words 不一致、または符号とゼロの境界検証)
                if (eng() % 2 == 0) {
                    mpz_set_ui(gmp_a, 0);
                    mpz_urandomb(gmp_b, gmp_state, bits_b);
                    if (mpz_sgn(gmp_b) == 0) mpz_set_ui(gmp_b, 1ULL);
                } else {
                    mpz_urandomb(gmp_a, gmp_state, bits_a);
                    if (mpz_sgn(gmp_a) == 0) mpz_set_ui(gmp_a, 1ULL);
                    mpz_set_ui(gmp_b, 0);
                }
                break;

            case 5: // 【戦略5】同一値から微小ノイズで末尾のワードだけを狂わせる (memcmp が途中で弾くケース)
                mpz_urandomb(gmp_a, gmp_state, bits_a);
                if (sign_dist(eng)) mpz_neg(gmp_a, gmp_a);
                mpz_set(gmp_b, gmp_a);
                if (eng() % 2 == 0) {
                    mpz_add_ui(gmp_b, gmp_b, 1ULL);
                } else {
                    mpz_sub_ui(gmp_b, gmp_b, 1ULL);
                }
                break;
        }

        // PFI2 にデータを充填
        [[maybe_unused]] bool ok_a = pfi_a.from_mpz(gmp_a);
        [[maybe_unused]] bool ok_b = pfi_b.from_mpz(gmp_b);
        assert(ok_a && ok_b);

        // オラクル予測: GMP が 0 を返せば等値（true）、それ以外は不等（false）
        bool expect_equal = (mpz_cmp(gmp_a, gmp_b) == 0);

        // ターゲット演算実行
        bool actual_equal_1 = (pfi_a == pfi_b);
        bool actual_equal_2 = (pfi_b == pfi_a); // 対称性の検証

        // 1. 一致性検証
        if (actual_equal_1 != expect_equal) {
            std::cerr << "CRITICAL FAILURE: operator== returned " << (actual_equal_1 ? "true" : "false")
                      << ", but expected " << (expect_equal ? "true" : "false") << " at iteration " << i << std::endl;
            gmp_printf("   gmp_a = %Zd\n", gmp_a);
            gmp_printf("   gmp_b = %Zd\n", gmp_b);
            std::exit(1);
        }

        // 2. 対称性の検証 (A == B ならば B == A であること)
        if (actual_equal_1 != actual_equal_2) {
            std::cerr << "CRITICAL FAILURE: operator== symmetry broken at iteration " << i << std::endl;
            gmp_printf("   gmp_a = %Zd\n", gmp_a);
            gmp_printf("   gmp_b = %Zd\n", gmp_b);
            std::exit(1);
        }

        // 3. 同一インスタンス比較の自己等価性検証 (this == &rhs ルートの直接通過)
        if (!(pfi_a == pfi_a) || !(pfi_b == pfi_b)) {
            std::cerr << "CRITICAL FAILURE: operator== self-equality failed at iteration " << i << std::endl;
            std::exit(1);
        }
    }

    mpz_clear(gmp_a);
    mpz_clear(gmp_b);
    gmp_randclear(gmp_state);

    std::cout << "-> Passed GMP Differential operator== Test successfully!" << std::endl;
}

// --- 2026年6月28日15時40分 ---

void run_add_sub_identity_test(std::size_t iterations) {
    std::cout
        << "Running: Add/Sub Identity Test ("
        << iterations
        << " iterations)..."
        << std::endl;

    std::random_device rd;
    uint64_t seed = rd();
    std::mt19937_64 eng(seed);

    std::cout
        << "   [Identity Seed]: 0x"
        << std::hex << seed
        << std::dec
        << std::endl;

    gmp_randstate_t gmp_state;
    gmp_randinit_default(gmp_state);
    gmp_randseed_ui(gmp_state, seed);

    constexpr std::size_t TEST_WORDS = 300;

    const unsigned long MAX_BITS =
        static_cast<unsigned long>(
            TEST_WORDS *
            PFI2::DIGITS_PER_WORD *
            std::log2(PFI2::BASE) * 0.8
        );

    std::uniform_int_distribution<unsigned long>
        bit_dist(0, MAX_BITS);

    mpz_t gmp_a;
    mpz_t gmp_b;

    mpz_init(gmp_a);
    mpz_init(gmp_b);

    for (std::size_t i = 0; i < iterations; ++i) {

        mpz_urandomb(gmp_a, gmp_state, bit_dist(eng));
        mpz_urandomb(gmp_b, gmp_state, bit_dist(eng));

        // ランダム符号
        if (eng() & 1) mpz_neg(gmp_a, gmp_a);
        if (eng() & 1) mpz_neg(gmp_b, gmp_b);

        PFI2 a(TEST_WORDS * 3);
        PFI2 b(TEST_WORDS * 3);

        assert(a.from_mpz(gmp_a));
        assert(b.from_mpz(gmp_b));

        //--------------------------------------------------
        // (a+b)-b == a
        //--------------------------------------------------

        PFI2 lhs1(TEST_WORDS * 3);
        assert(lhs1.assign_contents(a));

        bool ok1 = lhs1.add_inplace(b);

        if (!ok1) {
            std::cerr
                << "add_inplace failed in identity test"
                << std::endl;
            std::exit(1);
        }

        bool ok2 = lhs1.sub_inplace(b);

        if (!ok2) {
            std::cerr
                << "sub_inplace failed in identity test"
                << std::endl;
            std::exit(1);
        }

        if (!(lhs1 == a)) {
            std::cerr
                << "(a+b)-b != a"
                << std::endl;

            std::exit(1);
        }

        //--------------------------------------------------
        // (a-b)+b == a
        //--------------------------------------------------

        PFI2 lhs2(TEST_WORDS * 3);
        assert(lhs2.assign_contents(a));

        bool ok3 = lhs2.sub_inplace(b);

        if (!ok3) {
            std::cerr
                << "sub_inplace failed in identity test"
                << std::endl;
            std::exit(1);
        }

        bool ok4 = lhs2.add_inplace(b);

        if (!ok4) {
            std::cerr
                << "add_inplace failed in identity test"
                << std::endl;
            std::exit(1);
        }

        if (!(lhs2 == a)) {
            std::cerr
                << "(a-b)+b != a"
                << std::endl;

            std::exit(1);
        }

        //--------------------------------------------------
        // a + 0 = a
        //--------------------------------------------------

        {
            PFI2 x(TEST_WORDS * 3);
            PFI2 zero(TEST_WORDS * 3);

            assert(x.assign_contents(a));

            bool ok = x.add_inplace(zero);

            if (!ok) {
                std::cerr
                    << "a + 0 failed"
                    << std::endl;
                std::exit(1);
            }

            if (!(x == a)) {
                std::cerr
                    << "a + 0 != a"
                    << std::endl;
                std::exit(1);
            }
        }

        //--------------------------------------------------
        // a - 0 = a
        //--------------------------------------------------

        {
            PFI2 x(TEST_WORDS * 3);
            PFI2 zero(TEST_WORDS * 3);

            assert(x.assign_contents(a));

            bool ok = x.sub_inplace(zero);

            if (!ok) {
                std::cerr
                    << "a - 0 failed"
                    << std::endl;
                std::exit(1);
            }

            if (!(x == a)) {
                std::cerr
                    << "a - 0 != a"
                    << std::endl;
                std::exit(1);
            }
        }

        //--------------------------------------------------
        // a - a = 0
        //--------------------------------------------------

        {
            PFI2 x(TEST_WORDS * 3);

            assert(x.assign_contents(a));

            bool ok = x.sub_inplace(a);

            if (!ok) {
                std::cerr
                    << "a - a failed"
                    << std::endl;
                std::exit(1);
            }

            if (!x.is_zero()) {
                std::cerr
                    << "a - a != 0"
                    << std::endl;
                std::exit(1);
            }
        }

        //--------------------------------------------------
        // a + b = b + a
        //--------------------------------------------------

        {
            PFI2 lhs(TEST_WORDS * 3);
            PFI2 rhs(TEST_WORDS * 3);

            assert(lhs.assign_contents(a));
            assert(rhs.assign_contents(b));

            bool ok1 = lhs.add_inplace(b);
            bool ok2 = rhs.add_inplace(a);

            if (!ok1 || !ok2) {
                std::cerr
                    << "commutativity add failed"
                    << std::endl;
                std::exit(1);
            }

            if (!(lhs == rhs)) {
                std::cerr
                    << "a + b != b + a"
                    << std::endl;
                std::exit(1);
            }
        }
        //--------------------------------------------------
        // invariant
        //--------------------------------------------------

        if (!lhs1.invariant_holds()) {
            std::cerr
                << "Invariant failure lhs1"
                << std::endl;
            std::exit(1);
        }

        if (!lhs2.invariant_holds()) {
            std::cerr
                << "Invariant failure lhs2"
                << std::endl;
            std::exit(1);
        }
    }

    mpz_clear(gmp_a);
    mpz_clear(gmp_b);

    gmp_randclear(gmp_state);

    std::cout
        << "-> Passed Add/Sub Identity Test!"
        << std::endl;
}

// --- 2026年6月28日15時54分 ---



int main() {
    std::cout << "=== PFI2 Infrastructure Unit Tests ===" << std::endl;

    try {
        test_constructor_and_move();
        test_clear_and_zero();
        test_validation_and_bounds();
        test_delayed_initialization_and_zero_collapse();
        
        run_random_fuzz_test(100000);
        run_cmp_abs_fuzz_test(100000);
        //run_gmp_differential_add_test(40000);
        //run_gmp_differential_sub_test(40000);
        //run_gmp_differential_sub_from_test(40000);

        //run_self_alias_test(40000);

        //run_gmp_differential_add_inplace_test(40000);
        //run_gmp_differential_sub_inplace_test(40000);
        //run_gmp_differential_equal_test(40000);

        run_add_sub_identity_test(50000);

        std::cout << "\nALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    } catch (...) {
        std::cerr << "Unexpected exception caught during execution." << std::endl;
        return 1;
    }

    return 0;
}