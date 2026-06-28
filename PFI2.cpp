#include "PFI2.h"
#include <algorithm>
#include <cstring>
#include <immintrin.h>
#include <gmp.h>

// メモリ割り当てのヘルパー（無名名前空間内）
namespace {
void* aligned_alloc64(std::size_t bytes) noexcept {
#if defined(_MSC_VER) || defined(__MINGW32__)
    return _aligned_malloc(bytes, 64);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 64, bytes) != 0) return nullptr;
    return ptr;
#endif
}

void aligned_free64(void* ptr) noexcept {
#if defined(_MSC_VER) || defined(__MINGW32__)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}
} // namespace

// ==========================================
// GMP (mpz_t) との相互変換
// ==========================================

// PFI2 の現在の値を GMP の mpz_t へエクスポート
void PFI2::to_mpz(mpz_t rop) const noexcept {
    // 1. 自身が値として 0 なら、GMP 側も一瞬で 0 に設定して終了
    if (this->is_zero()) {
        mpz_set_ui(rop, 0);
        return;
    }

    // 2. ホーナー法 (Horner's method) による多倍長組み立て
    // 桁位置の最も高い（最上位ワードの最上位パート）から下に向かって、
    // 「暫定値 * BASE + 次のパートの値」を繰り返す。
    mpz_set_ui(rop, 0);

    // 有効ワード数（active_words_）のトップからデクリメント走査
    std::size_t w = active_words_;
    while (w > 0) {
        --w;
        uint64_t d0, d1, d2;
        unpack_word(words_[w], d0, d1, d2);

        // 1ワード内は d0, d1, d2 の順にパックされている（d2が上位桁側）
        // したがって、蓄積演算の順序は d2 -> d1 -> d0 となる
        mpz_mul_ui(rop, rop, BASE);
        mpz_add_ui(rop, rop, d2);

        mpz_mul_ui(rop, rop, BASE);
        mpz_add_ui(rop, rop, d1);

        mpz_mul_ui(rop, rop, BASE);
        mpz_add_ui(rop, rop, d0);
    }

    // 3. 最後に符号を反映（値が 0 の時は 2 の時点で正の 0 になっている）
    if (is_negative_) {
        mpz_neg(rop, rop);
    }
}

// GMP の mpz_t の値を 5^8 進数に分解して PFI2 へインポート
// 最適化はされていない
bool PFI2::from_mpz(const mpz_t op) noexcept {
    // 1. 事前クリア（有効サイズを0に落とし、生ゴミ許容ポリシーを敷く）
    this->clear();

    // 入力が 0 なら初期化成功として即座に終了
    if (mpz_sgn(op) == 0) {
        return true;
    }

    // 2. 破壊的演算を避けるため、作業用に絶対値のコピーを作成
    mpz_t tmp;
    mpz_init_set(tmp, op);
    mpz_abs(tmp, tmp);

    // 3. 連続剰余算による5^8進数への連続分解
    std::size_t digit_idx = 0;
    bool success = true;

    while (mpz_sgn(tmp) > 0) {
        // 物理キャパシティ（最大桁数）を超えそうな場合は、バッファオーバーフローを防御して失敗を返す
        std::size_t word_idx = digit_idx / DIGITS_PER_WORD;
        if (word_idx >= capacity_words_) {
            success = false;
            break;
        }

        // tmp を BASE (390625) で割り、商を tmp に上書きし、余り（rem）を得る
        // mpz_fdiv_q_ui は C言語の unsigned long を返すため、下位20bitのマスク処理等も不要で安全
        unsigned long rem = mpz_fdiv_q_ui(tmp, tmp, BASE);

        // O(1) の set_digit を用いて遅延初期化とパッキング、および active_words_ の伸長を自動委譲
        if (!this->set_digit(digit_idx, static_cast<uint64_t>(rem))) {
            success = false;
            break;
        }

        ++digit_idx;
    }

    // GMPの作業用メモリを解放（C ABI のため必須）
    mpz_clear(tmp);

    if (!success) {
        this->clear(); // 失敗時はオブジェクトの状態を安全な 0 に戻す
        return false;
    }

    // 4. 符号の同期
    if (mpz_sgn(op) < 0) {
        is_negative_ = true;
    }

    return true;
}

// ==========================================
// 不変条件バリデーション（デバッグ用）
// ==========================================

bool PFI2::invariant_holds() const noexcept {
    // 1. ポインタと容量の基本整合性チェック
    if (capacity_words_ == 0 && words_ == nullptr) return true;
    if (capacity_words_ > 0 && words_ == nullptr) return false;
    
    // 2. 有効ワード数が物理容量を超えていないか
    if (active_words_ > capacity_words_) return false;

    // 3. 値が「0」の時は、符号が必ず「正(false)」にリセットされているか
    if (is_zero() && is_negative_) return false;

    // 4. 有効ビット内の値の正当性チェック
    for (std::size_t w = 0; w < active_words_; ++w) {
        // 1ワードあたり PART_BITS bit * 3 = 60bit なので、残り 4bit (最上位ビット側) は常に 0 でなければならない
        if ((words_[w] >> (PART_BITS * 3)) != 0) return false;
        
        uint64_t d0, d1, d2;
        unpack_word(words_[w], d0, d1, d2);
        
        // 各桁が 5^8 = 390625 未満であることを厳格にチェック
        if (d0 >= BASE || d1 >= BASE || d2 >= BASE) return false;
    }

    // 5. 冗長性の検証：active_words_ が 0 でないなら、最上位ワードは必ず非ゼロでなければならない
    // （0 なのに active_words_ にカウントされている状態は、トリム漏れのバグ）
    if (active_words_ > 0 && words_[active_words_ - 1] == 0) return false;

    return true;
}

// ==========================================
// コンストラクタ / デストラクタ / ムーブ
// ==========================================

// コンストラクタに渡すのは "5^8進数で何桁か" という情報であることに注意 (words数ではない)
PFI2::PFI2(std::size_t max_digits)
    : words_(nullptr),
      capacity_words_((max_digits + DIGITS_PER_WORD - 1) / DIGITS_PER_WORD),
      active_words_(0),
      is_negative_(false)
{
    if (capacity_words_ == 0) return;

    // AVX-512 やアラインドロードを安全に回せるよう 64バイトアライメントで確保のみ行う
    words_ = static_cast<uint64_t*>(aligned_alloc64(sizeof(uint64_t) * capacity_words_));
    if (!words_) {
        capacity_words_ = 0;
        active_words_ = 0;
        return;
    }
    
    // 【最速化】std::memset は完全に排除。生メモリのまま即座にリターンする。
}

PFI2::~PFI2() { 
    aligned_free64(words_); 
}

PFI2::PFI2(PFI2&& other) noexcept
    : words_(other.words_),
      capacity_words_(other.capacity_words_),
      active_words_(other.active_words_),
      is_negative_(other.is_negative_)
{
    // 移動元のリソースを安全に解放・初期化
    other.words_ = nullptr;
    other.capacity_words_ = 0;
    other.active_words_ = 0;
    other.is_negative_ = false;
}

PFI2& PFI2::operator=(PFI2&& other) noexcept {
    if (this == &other) return *this;

    // 自身が現在持っている物理メモリを安全に解放
    aligned_free64(words_);

    // 各状態ポインタ・変数を完全泥棒ムーブ
    words_ = other.words_;
    capacity_words_ = other.capacity_words_;
    active_words_ = other.active_words_;
    is_negative_ = other.is_negative_;

    // 移動元の状態を安全に初期化
    other.words_ = nullptr;
    other.capacity_words_ = 0;
    other.active_words_ = 0;
    other.is_negative_ = false;

    return *this;
}

// オブジェクト複製
// 容量変更あり
// 再確保あり
void PFI2::clone_from(const PFI2& other) {
    if (this == &other) return;

    // キャパシティが合わない場合は割り当て（再現性の確保のためベンチマークでは同サイズ前提）
    if (this->capacity_words_ != other.capacity_words_) {
        aligned_free64(this->words_);
        this->capacity_words_ = other.capacity_words_;
        if (this->capacity_words_ > 0) {
            this->words_ = static_cast<uint64_t*>(aligned_alloc64(sizeof(uint64_t) * this->capacity_words_));
        } else {
            this->words_ = nullptr;
        }
    }

    this->active_words_ = other.active_words_;
    this->is_negative_ = other.is_negative_;

    // 有効ワード分、またはキャパシティ分を一括物理コピー
    if (this->words_ && other.words_ && this->active_words_ > 0) {
        std::memcpy(this->words_, other.words_, sizeof(uint64_t) * this->active_words_);
    }
}

// rhs の内容を this にコピーする。
// capacity_words_ は変更しない。
// capacity_words_ < rhs.active_words_ の場合は false。
bool PFI2::assign_contents(const PFI2& rhs) noexcept {
    if (this == &rhs) return true;

    if (this->capacity_words_ < rhs.active_words_) return false;

    std::memcpy(
        this->words_,
        rhs.words_,
        sizeof(uint64_t)*rhs.active_words_
    );

    this->active_words_ = rhs.active_words_;
    this->is_negative_  = rhs.is_negative_;

    return true;
}
// rhs の内容を this にコピーする。
// capacity_words_ と is_negative_ は変更しない。
// capacity_words_ < rhs.active_words_ の場合は false。
bool PFI2::assign_abs_contents(const PFI2& rhs) noexcept {
    if (this == &rhs) return true;

    if (this->capacity_words_ < rhs.active_words_) return false;

    std::memcpy(
        this->words_,
        rhs.words_,
        sizeof(uint64_t) * rhs.active_words_
    );

    this->active_words_ = rhs.active_words_;

    return true;
}

// ==========================================
// クリア、および基本状態判定
// ==========================================

void PFI2::clear() noexcept {
    // メモリには一切触らない。
    // 有効ワード数を 0 に落とし、符号を正にリセットするだけの爆速 O(1) 処理。
    active_words_ = 0;
    is_negative_ = false;
}

bool PFI2::is_zero() const noexcept {
    // invariant_holds() が「値が0のときは active_words_ == 0」かつ
    // 「active_words_ > 0 なら最上位ワードは非ゼロ」を保証しているため、
    // 単なる整数比較 1 回のみの O(1) で完全判定できる。
    return active_words_ == 0;
}

// BASE^digit_index の位の値を返す
uint64_t PFI2::get_digit(std::size_t digit_index) const noexcept {
    std::size_t word_idx;
    unsigned bit_offset;
    
    // 必要な情報だけを O(1) でスマートに導出
    resolve_index(digit_index, word_idx, bit_offset);

    // 有効サイズ（active_words_）の外側ならメモリロードせず即座に 0
    if (word_idx >= active_words_) {
        return 0;
    }

    return (words_[word_idx] >> bit_offset) & PART_MASK;
}

// BASE^digit_index の位を指定された値にsetする (0-indexed な桁位置への代入)
bool PFI2::set_digit(std::size_t digit_index, uint64_t value) noexcept {
    // 1. 【入力バリデーション】BASE 以上の不正な値が渡された場合は、
    // 内部構造（不変条件）を破壊しないよう、書き込みを拒否して即座に false を返す。
    if (value >= BASE) {
        return false;
    }

    std::size_t word_idx;
    unsigned bit_offset;
    resolve_index(digit_index, word_idx, bit_offset);

    // 2. 物理容量（capacity_words_）を超える位置への書き込みは、
    // 静的メモリ契約に基づき、書き込みを拒否して false を返す（または処理能力を超えたシグナル）。
    if (word_idx >= capacity_words_) {
        return false;
    }

    if (word_idx >= active_words_) {
        // 3. 書き込み先が現在の有効範囲の外側（＝ゴミ地帯）である場合
        // 正常な値の範囲（value < BASE）において、value == 0 なら上位ゼロ拡張と同じなので何もしない
        if (value == 0) return true;

        // 隙間の未初期化領域を安全に 0 で遅延初期化
        for (std::size_t w = active_words_; w < word_idx; ++w) {
            words_[w] = 0;
        }

        // ゴミデータの上から新しい値だけでパックして完全上書き
        words_[word_idx] = value << bit_offset; // 入力バリデーションにより value & PART_MASK は不要

        // 有効ワード数を一気に引き伸ばす
        active_words_ = word_idx + 1;
    } else {
        // 4. 書き込み先がすでに有効な範囲内である場合
        uint64_t mask = ~(PART_MASK << bit_offset);
        uint64_t next_word = (words_[word_idx] & mask) | (value << bit_offset);
        
        // 最上位ワードが 0 に収束する場合
        if (word_idx == active_words_ - 1 && next_word == 0) {
            // メモリへのストアをサボり、有効サイズをデクリメントして下方に 0 をスキャン
            --active_words_;
            while (active_words_ > 0 && words_[active_words_ - 1] == 0) {
                --active_words_;
            }
        } else {
            words_[word_idx] = next_word;
        }
    }

    return true;
}

// |this| - |rhs| の符号を返す。返り値は -1(負), 0 ,1(正)
int PFI2::cmp_abs(const PFI2& rhs) const noexcept {
    // 1. 【最速パス】有効ワード数が異なれば、大きい方が絶対に強い。
    // メモリを1バイトもロードせず、レジスタの整数比較だけで O(1) 即決する。
    if (active_words_ > rhs.active_words_) return 1;
    if (active_words_ < rhs.active_words_) return -1;

    // 2. 有効ワード数が完全に一致している場合
    // 両方とも値として 0 の場合は、走査の必要すらなく等しい。
    if (active_words_ == 0) return 0;

    // 3. 同一サイズ（active_words_ > 0）の場合のみ、実際の最上位ワードから下に向かって走査
    // ループカウンタのアンダーフローを綺麗に回避する while パターン
    std::size_t w = active_words_;
    while (w > 0) {
        --w;
        uint64_t lw = words_[w];
        uint64_t rw = rhs.words_[w];

        if (lw != rw) {
            // 昔のコードのコメント通り、1ワード内は下位側から d0, d1, d2（d2が最上位ビット側）とパックされているため、
            // 単純な 64bit 整数としての大小比較が、そのまま5進数の大小関係と完全に一致する。
            return (lw > rw) ? 1 : -1;
        }
    }

    return 0; // すべてのワードが完全一致
}

// this - rhs の符号を返す。返り値は -1(負), 0, 1(正)
int PFI2::cmp(const PFI2& rhs) const noexcept {
    // 1. 【最速パス】どちらも（あるいは片方が）「値として 0」の場合の処理
    // is_zero() は active_words_ == 0 の整数比較1回なので極めて軽い。
    bool lhs_zero = is_zero();
    bool rhs_zero = rhs.is_zero();

    if (lhs_zero && rhs_zero) return 0; // ともに 0 なら符号に関わらず完全に等しい
    if (lhs_zero) return rhs.is_negative_ ? 1 : -1; // 0 と 相手の比較（相手が負なら 0 の勝ち）
    if (rhs_zero) return is_negative_ ? -1 : 1; // 相手が 0 と 自分の比較（自分が負なら 0 の負け）

    // 2. 【最速パス2】どちらも非ゼロで、符号が異なる場合
    // この時点で互いに 0 ではないことが確定しているため、符号が違えば走査なしで一瞬で確定する。
    if (is_negative_ != rhs.is_negative_) {
        return is_negative_ ? -1 : 1;   // 自分が負なら相手の勝ち(-1) そうでないなら自分の勝ち(1)
    }

    // 3. 同符号（かつ共に非ゼロ）の場合：ここで初めて絶対値比較のメモリ走査を呼び出す
    int res_abs = cmp_abs(rhs);

    // 両方正なら絶対値の大小がそのまま結果。両方負なら結果の符号を反転（-res_abs）。
    return is_negative_ ? -res_abs : res_abs;
}

bool PFI2::operator==(const PFI2& rhs) const noexcept {
    if (this == &rhs) {
        return true;
    }

    if (active_words_ != rhs.active_words_) {
        return false;
    }

    if (is_negative_ != rhs.is_negative_) {
        return false;
    }

    if (active_words_ == 0) {
        return true;
    }

    return std::memcmp(
        words_,
        rhs.words_,
        active_words_ * sizeof(uint64_t)
    ) == 0;
}

void PFI2::neg() noexcept {
    // 値が 0 の場合は、符号を反転させると不変条件（0は必ず正）を破るため、
    // 何もせずに正常終了（true）とする。
    if (this->is_zero()) {
        //return true;
        return;
    }

    // 非ゼロのときのみ符号ビットを反転
    is_negative_ = !is_negative_;

    // 安全弁アサート
    //assert(this->invariant_holds());
    //return true;
    return;
}

// |this| <- |this| + |rhs|
// 符号は変更しない
// 条件:
//   結果のワード数 <= capacity(this)
// 条件違反時は false を返す。
// this の内容は保持されない。
bool PFI2::add_abs_inplace(const PFI2& rhs) noexcept {
    // 相手が値として 0 なら、何も加算する必要がないので O(1) で即座に終了
    if (rhs.is_zero()) return true;

    std::size_t lhs_active = active_words_;
    std::size_t rhs_active = rhs.active_words_;

    // 【フロントガード】相手の有効ワード数が、こちらの物理容量上限を超えている場合は即座にオーバーフロー
    if (rhs_active > capacity_words_) {
        return false;
    }

    uint64_t carry = 0;
    std::size_t w = 0;
    std::size_t min_active = std::min(lhs_active, rhs_active);

    // ----------------------------------------------------------------
    // フェーズ 1: 共通区間の一括加算（1ループ完全密結合・レジスタ完結型）
    // ----------------------------------------------------------------
    uint64_t* const __restrict l_words = words_;
    const uint64_t* const __restrict r_words = rhs.words_;

    for (; w < min_active; ++w) {
        // 1. ロードは1回だけ。相手のデータと前のキャリーを、レジスタ上で一気に合成する
        // 各パート最大 390624。 (390624 * 2) + 1 = 781249 < 2^20 (1048576) なので、
        // 20bit境界を超えて隣のパートを汚染することは絶対にない！
        uint64_t sum_word = l_words[w] + r_words[w];

        // 2. レジスタ上の値（sum_word）から純粋な各パートを抽出
        uint64_t d0 = sum_word & PART_MASK;
        uint64_t d1 = (sum_word >> PART_BITS) & PART_MASK;
        uint64_t d2 = (sum_word >> PART_BITS_TIMES_TWO) & PART_MASK;

        // 3. 前のループから引き継いだ carry を最下位パートに合算
        d0 += carry;

        // 4. 下から順に BASE 超過チェックを入れてドミノを回す
        // （コンパイラはこれを綺麗に cmov 命令に落とし込む）
        if (d0 >= BASE) { d0 -= BASE; d1 += 1; }
        if (d1 >= BASE) { d1 -= BASE; d2 += 1; }
        if (d2 >= BASE) { d2 -= BASE; carry = 1; } else { carry = 0; }

        // 5. 補正完了データをパッキングして「1回だけ」書き戻す
        l_words[w] = pack_word(d0, d1, d2);
    }
    // この状態では w = min_activeになっている。
    // carryが0なら、この分までの足し算は安全に完了している。

    // ----------------------------------------------------------------
    // フェーズ 2: 残存区間の高速処理（active_words_ の大小トポロジーで分岐）
    // ----------------------------------------------------------------
    if (lhs_active > rhs_active) {
        // --- ケース A: 自分の方がデカい（相手側はすべて数学的 0） ---
        if (carry != 0) {
            // carryがある場合
            // 【SKIP_SIGNAL_WORD ハック】一撃で全桁 0 になる自分のワードを高速一括クリア
            while (w < lhs_active && words_[w] == SKIP_SIGNAL_WORD) {
                words_[w] = 0ULL;
                ++w;
            }

            // スキップを抜けた直後の1ワードでキャリーを完全に吸収
            if (w < lhs_active) {
                uint64_t d0, d1, d2;
                unpack_word(words_[w], d0, d1, d2);
                
                d0 += carry; // carry は必ず 1
                do {
                    if (d0 >= BASE) {
                        d0 -= BASE;
                        d1 += 1;
                    } else {
                        carry = 0;
                        break;
                    }
                    if (d1 >= BASE) {
                        d1 -= BASE;
                        d2 += 1;
                    } else {
                        carry = 0;
                        break;
                    }
                    if (d2 >= BASE) {
                        d2 -= BASE;
                        carry = 1;
                    } else {
                        carry = 0;
                        break;
                    }
                } while (false);

                words_[w] = pack_word(d0, d1, d2);
                ++w;    // いちおう
            }
        }
        // キャリーが 0 に収束した場合、それより上の残りの自分自身のワードは変更不要。
        //探索範囲（w）の安全弁として、w を lhs_active まで強制的に進める
        if (carry == 0) {
            w = std::max(w, lhs_active);
        }
    } else if (rhs_active > lhs_active) {
        // --- ケース B: 相手の方がデカい（自分側は数学的 0、しかし物理的には生ゴミ） ---
        if (carry != 0) {
            // 相手（rhs）側の SKIP_SIGNAL_WORD をスキャンして高速スキップ（自分側は 0 クリア）
            while (w < rhs_active && rhs.words_[w] == SKIP_SIGNAL_WORD) {
                words_[w] = 0ULL; // 数学的にも 0 + SKIP + 1 = 0 繰り上がり1
                ++w;
            }

            // スキップを抜けた直後の1ワードでキャリー処理
            if (w < rhs_active) {
                uint64_t d0, d1, d2;
                unpack_word(rhs.words_[w], d0, d1, d2);

                d0 += carry; // carry は必ず 1
                do {
                    if (d0 >= BASE) {
                        d0 -= BASE;
                        d1 += 1;
                    } else {
                        carry = 0;
                        break;
                    }
                    if (d1 >= BASE) {
                        d1 -= BASE;
                        d2 += 1;
                    } else {
                        carry = 0;
                        break;
                    }
                    if (d2 >= BASE) {
                        d2 -= BASE;
                        carry = 1;
                    } else {
                        carry = 0;
                        break;
                    }
                } while (false);
                
                words_[w] = pack_word(d0, d1, d2);
                ++w;    // いちおう
            }
        }

        // 【究極のサボりパス】キャリーが消えたら、残りの rhs 区間を memcpy で一括超高速コピー！
        // 計算も、パッキングも、生ゴミのクリーンアップすらもすべてスキップする。
        if (w < rhs_active && carry == 0) {
            std::memcpy(&words_[w], &rhs.words_[w], (rhs_active - w) * sizeof(uint64_t));
            w = rhs_active;
        }
    }

    // ----------------------------------------------------------------
    // フェーズ 3: 最終キャリー処理とオーバーフロー防御
    // ----------------------------------------------------------------
    if (carry != 0) {
        if (w >= capacity_words_) {
            // 最上位を突き抜けたのに、格納する物理ワードスペースがない場合はオーバーフロー
            return false;
        }
        // 新しい最上位ワードにキャリーの「1」を格納
        words_[w] = pack_word(1, 0, 0);
        ++w;
    }

    // ----------------------------------------------------------------
    // フェーズ 4: active_words_ の厳密な更新
    // ----------------------------------------------------------------
    active_words_ = w;
    return true;
}

// |this| <- |this| - |rhs|
// 符号は変更しない
// 条件:
//   |this| >= |rhs|
// 条件違反時は false を返す。
// this の内容は保持されない。
bool PFI2::sub_abs_inplace(const PFI2& rhs) noexcept {
    if (rhs.is_zero()) return true;

    std::size_t lhs_active = active_words_;
    std::size_t rhs_active = rhs.active_words_;

    // 絶対値の大小関係（|this| >= |rhs|）の前提が壊れている場合は即座に終了
    // 完全にとらえきれているわけではない
    if (rhs_active > lhs_active) {
        return false;
    }

    uint64_t borrow = 0;
    std::size_t w = 0;
    std::size_t min_active = rhs_active; // 少なくとも右辺の桁分は動かないといけない

    // 各スロットに BASE を配置したマジックワード
    constexpr uint64_t OFFSET_WORD = BASE | (BASE << PART_BITS) | (BASE << PART_BITS_TIMES_TWO);
    constexpr uint64_t FULL_DIGIT = BASE - 1;

    uint64_t* const __restrict l_words = words_;
    const uint64_t* const __restrict r_words = rhs.words_;

    // ----------------------------------------------------------------
    // フェーズ 1: 共通区間の一括減算（1ループ完全密結合・レジスタ完結型）
    // ----------------------------------------------------------------
    for (; w < min_active; ++w) {
        // OFFSET_WORD を噛ませて各パート内の引き算の一時的なアンダーフローを防ぐ
        uint64_t diff_word = l_words[w] + OFFSET_WORD - r_words[w] - borrow;
        
        uint64_t d0 = diff_word & PART_MASK;
        uint64_t d1 = (diff_word >> PART_BITS) & PART_MASK;
        uint64_t d2 = (diff_word >> PART_BITS_TIMES_TWO) & PART_MASK;

        // コンパイラに綺麗な cmov を吐かせるシンプルな if ドミノ
        if (d0 >= BASE) { d0 -= BASE; } else { d1 -= 1; }
        if (d1 >= BASE) { d1 -= BASE; } else { d2 -= 1; }
        if (d2 >= BASE) { d2 -= BASE; borrow = 0; } else { borrow = 1; }

        l_words[w] = pack_word(d0, d1, d2);
    }

    // ----------------------------------------------------------------
    // フェーズ 2: 自身のボロー残存区間の高速伝播（検証された do-while 型）
    // ----------------------------------------------------------------
    if (borrow != 0) {
        // 【0ULL スキップハック】
        // 全桁が 0 のワードから 1 引くと、一撃で全桁最大値（SKIP_SIGNAL_WORD）に化ける
        while (w < lhs_active && l_words[w] == 0ULL) {
            l_words[w] = SKIP_SIGNAL_WORD;
            ++w;
        }

        // スキップを抜けた直後の1ワードでボローを完全に吸収
        if (w < lhs_active) {
            uint64_t d0, d1, d2;
            unpack_word(l_words[w], d0, d1, d2);

            // 君の発見した、統計的・アセンブリレベルで最強の早期脱出トポロジー
            do {
                // 第0桁で借りが返せれば、d1 や d2 の計算命令を一切触らずに即ブレイク
                if (d0 >= borrow) {
                    d0 -= borrow;
                    borrow = 0;
                    break;
                } else {
                    d0 = FULL_DIGIT;
                    borrow = 1;
                }

                // 第1桁
                if (d1 >= borrow) {
                    d1 -= borrow;
                    borrow = 0;
                    break;
                } else {
                    d1 = FULL_DIGIT;
                    borrow = 1;
                }

                // 第2桁
                if (d2 >= borrow) {
                    d2 -= borrow;
                    borrow = 0;
                } else {
                    d2 = FULL_DIGIT;    // 借りが上に溢れない限り実行されない
                    borrow = 1;
                }
            } while (false);

            l_words[w] = pack_word(d0, d1, d2);
            ++w;
        }
    }

    // ボローが 0 に収束した場合、それより上の残りの自分のワードは変更不要。
    if (borrow == 0) {
        //w = std::max(w, lhs_active);
        w = lhs_active;
    } else {
        // 最終的に borrow が残ってしまった場合は、前提条件（|this| >= |rhs|）に
        // 違反していた（アンダーフローした）ことを意味するので false を返す
        return false;
    }

    // ----------------------------------------------------------------
    // フェーズ 3: 厳密な active_words_ の更新（最上位のゼロ・コラプス）
    // ----------------------------------------------------------------
    // 減算によって上位ワードが 0 になった場合、active_words_ を物理的に縮退させる
    while (w > 0 && l_words[w - 1] == 0ULL) {
        --w;
    }

    active_words_ = w;

    return true;
}

// |this| <- |rhs| - |this|
// 符号は変更しない
// 条件:
//   |rhs| >= |this|
//   active_words(rhs) <= capacity(this)
// 条件違反時は false を返す。
// this の内容は保持されない。
bool PFI2::sub_abs_from_inplace(const PFI2& rhs) noexcept {
    if (this->is_zero()) {
        return this->assign_abs_contents(rhs);
    }

    std::size_t rhs_active = rhs.active_words_;
    if (this->capacity_words_ < rhs_active || this->active_words_ > rhs.active_words_) {
        return false;
    }

    std::size_t lhs_active = active_words_;

    uint64_t borrow = 0;
    std::size_t w = 0;
    std::size_t min_active = lhs_active;    // 少なくとも左辺(this)の桁の分は動かないといけない
    
    // 各スロットに BASE を配置したマジックワード
    constexpr uint64_t OFFSET_WORD = BASE | (BASE << PART_BITS) | (BASE << PART_BITS_TIMES_TWO);
    constexpr uint64_t FULL_DIGIT = BASE - 1;

    uint64_t* const __restrict l_words = words_;
    const uint64_t* const __restrict r_words = rhs.words_;

    // 共通区間の一括減算
    for (; w < min_active; ++w) {
        
        uint64_t diff_word = r_words[w] + OFFSET_WORD - l_words[w] - borrow;

        uint64_t d0 = diff_word & PART_MASK;
        uint64_t d1 = (diff_word >> PART_BITS) & PART_MASK;
        uint64_t d2 = (diff_word >> PART_BITS_TIMES_TWO) & PART_MASK;

        if (d0 >= BASE) { d0 -= BASE; } else { d1 -= 1; }
        if (d1 >= BASE) { d1 -= BASE; } else { d2 -= 1; }
        if (d2 >= BASE) { d2 -= BASE; borrow = 0; } else { borrow = 1; }

        l_words[w] = pack_word(d0, d1, d2);
    }

    // 自身のボロー残存区間の伝播
    if (borrow != 0) {
        // borrowが残っていて、this.active_words_までは筆算をしている -> thisはもうこれで終わり
        while (w < rhs_active && r_words[w] == 0ULL) {
            l_words[w] = SKIP_SIGNAL_WORD;
            ++w;
        }

        // r_wordsが0でなければ、borrowは1なのだからどこかで借りを返せる
        if (w < rhs_active) {
            uint64_t d0, d1, d2;
            unpack_word(r_words[w], d0, d1, d2);

            do {
                // 第0桁で借りが返せれば、d1 や d2 の計算命令を一切触らずに即break
                if (d0 >= borrow) {
                    d0 -= borrow;
                    borrow = 0;
                    break;
                } else {
                    d0 = FULL_DIGIT;
                    borrow = 1;
                }

                // 第1桁
                if (d1 >= borrow) {
                    d1 -= borrow;
                    borrow = 0;
                    break;
                } else {
                    d1 = FULL_DIGIT;
                    borrow = 1;
                }

                // 第2桁
                if (d2 >= borrow) {
                    d2 -= borrow;
                    borrow = 0;
                } else {
                    d2 = FULL_DIGIT;    // 借りが上に溢れない限り実行されない
                    borrow = 1;
                }
            } while (false);
            
            l_words[w] = pack_word(d0, d1, d2);
            ++w;
        }
    }

    // borrowが0にならなかった場合、|rhs|>=|this|に違反していたということ
    // -> |rhs| < |this|
    if (borrow != 0) {
        return false;
    }

    // rhsの残りの部分をコピー
    for (; w < rhs_active; ++w) {
        l_words[w] = r_words[w];
    }

    // 厳密な active_words_ の更新
    w = rhs_active;
    while (w > 0 && l_words[w - 1] == 0ULL) {
        --w;
    }

    active_words_ = w;
    return true;
}

// this ← this + rhs
// 符号付き加算。
// 結果が this の capacity_words_ に収まる場合のみ成功する。
// 戻り値:
//   true  : 成功
//   false : 容量不足などにより失敗
//
// 失敗時の this の内容は保証しない。
bool PFI2::add_inplace(const PFI2& rhs) noexcept {
    // どちらかが 0 の場合は、O(1) で即時脱出（確実に成功）
    if (rhs.is_zero()) {
        return true;
    }
    if (this->is_zero()) {
        return this->assign_contents(rhs);
    }

    if (this->is_negative_ == rhs.is_negative_) {
        // ---------------------------------------------------------
        // トポロジー1: 同符号 (正 + 正、または 負 + 負)
        // ---------------------------------------------------------
        // 絶対値をそのまま加算。アンダーフローは起きないので確実に成功。
        //  |this| + |rhs| = sign(this)(|this|+|rhs|)   thisが正
        // -|this| - |rhs| = sign(this)(|this|+|rhs|)   thisが負
        return this->add_abs_inplace(rhs);
    }
    
    // ---------------------------------------------------------
    // トポロジー2: 異符号 (正 + 負、または 負 + 正)
    // ---------------------------------------------------------
    // 事前に絶対値の大小関係をスカウティング
    int cmp = this->cmp_abs(rhs);

    if (cmp > 0) {
        // ケース 2-a: |this|(正) > |rhs|(負)
        // 確実に引ききれることが保証されているので、カーネルを安全に叩く
        // this + rhs = |this| - |rhs| > 0
        return this->sub_abs_inplace(rhs);
    }

    if (cmp == 0) {
        // ケース 2-b: |this| == |rhs|
        // 絶対値が同じで異符号なので、結果は完全に 0
        this->clear(); // active_words_ = 0, is_negative_ = false
        return true;
    }

    // ---------------------------------------------------------
    // ケース 2-c: |this|(負) < |rhs|(正) (主客転転)
    // ---------------------------------------------------------
    // この関数（add_inplace）のキャパシティやコピー禁止制約では
    // this を汚染せずにインプレースで結果を保持することができない。
    // 変な補正をここでベタ書きせず、仕様通り「普通に false で返す」
    // this + rhs = -|this| + |rhs| = |rhs| - |this| < 0
    bool ok = this->sub_abs_from_inplace(rhs);

    if (ok) {
        this->is_negative_ = rhs.is_negative_;
    }

    return ok;

}

bool PFI2::sub_inplace(const PFI2& rhs) noexcept {
    if (rhs.is_zero()) return true;

    if (this->is_zero()) {
        bool ok = this->assign_abs_contents(rhs);

        if (ok && !this->is_zero()) {
            this->is_negative_ = !rhs.is_negative_;
        }

        return ok;
    }

    // 異符号
    if (this->is_negative_ != rhs.is_negative_) {
        //  |this(正)| + |rhs(負)|
        // -|this(負)| - |rhs(正)| = sign(this)(|this| + |rhs|)
        return this->add_abs_inplace(rhs);
    }

    // 同符号
    int cmp = this->cmp_abs(rhs);

    if (cmp > 0) {
        // |this| > |rhs|
        //  |this(正)| - |rhs(正)| > 0
        // -|this(負)| + |rhs(負)| = sign(this)(|this| - |rhs|) < 9
        return this->sub_abs_inplace(rhs);
    }

    if (cmp == 0) {
        // |rhs| == |this|
        // this - rhs = 0
        this->clear();
        return true;
    }

    // |this| < |rhs|
    //  |this(正)| - |rhs(正)| = -sign(this)(|rhs| - |this|)
    // -|this(負)| + |rhs(負)| = -sign(this)(|rhs| - |this|)
    bool ok = this->sub_abs_from_inplace(rhs);

    if (ok) {
        this->is_negative_ = !this->is_negative_;
    }

    return ok;
}