#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <gmp.h>

enum class PFI2MulAlgo {
    Schoolbook,
    Karatsuba,
    Toom3,
    Auto
};

class PFI2 {
public:
    static constexpr uint64_t BASE = 390625ULL; // 5^8
    static constexpr uint64_t MAX_DIGIT = BASE - 1ULL;
    
    // --- ビットパッキング用定数（集約） ---
    static constexpr unsigned PART_BITS = 20;
    static constexpr unsigned PART_BITS_TIMES_TWO = PART_BITS * 2; // [NEW] 2*PART_BITSを消し去る
    static constexpr uint64_t PART_MASK = (1ULL << PART_BITS) - 1ULL;
    static constexpr unsigned DIGITS_PER_WORD = 3;
    
    // SKIP_SIGNAL_WORD も定数を使って再定義
    static constexpr uint64_t SKIP_SIGNAL_WORD = MAX_DIGIT | (MAX_DIGIT << PART_BITS) | (MAX_DIGIT << PART_BITS_TIMES_TWO);

    static constexpr std::size_t ALIGNMENT = 64;

    explicit PFI2(std::size_t max_digits);
    ~PFI2();

    // GMP (mpz_t) との互換
    void to_mpz(mpz_t rop) const noexcept;
    [[nodiscard]] bool from_mpz(const mpz_t op) noexcept;

    // コピー禁止、ムーブ許可
    PFI2(const PFI2&) = delete;
    PFI2& operator=(const PFI2&) = delete;

    PFI2(PFI2&& other) noexcept;
    PFI2& operator=(PFI2&& other) noexcept;
    void copy_from(const PFI2& other);

    // --- 状態操作・アクセッサ ---
    void clear() noexcept;
    [[nodiscard]] bool is_zero() const noexcept;
    [[nodiscard]] bool is_negative() const noexcept { return is_negative_; }
    void set_negative(bool neg) noexcept { is_negative_ = neg; }
    void neg() noexcept;

    [[nodiscard]] std::size_t capacity_words() const noexcept { return capacity_words_; }
    [[nodiscard]] std::size_t active_words() const noexcept { return active_words_; }

    // --- 桁操作（ここに低レベルインデックスがインライン展開される） ---
    [[nodiscard]] uint64_t get_digit(std::size_t digit_index) const noexcept;
    [[nodiscard]] bool set_digit(std::size_t digit_index, uint64_t value) noexcept;

    // --- 比較演算 ---
    [[nodiscard]] int cmp(const PFI2& rhs) const noexcept;
    [[nodiscard]] int cmp_abs(const PFI2& rhs) const noexcept;

    // --- 絶対値を取った加算・減算 ---
    [[nodiscard]] bool add_abs_inplace(const PFI2& rhs) noexcept;
    [[nodiscard]] bool sub_abs_inplace(const PFI2& rhs) noexcept;
    [[nodiscard]] bool sub_abs_from_inplace(const PFI2& rhs) noexcept;

    // --- 符号付き加算・減算 ---
    [[nodiscard]] bool add_inplace(const PFI2& rhs) noexcept;

    // バリデーション
    [[nodiscard]] bool invariant_holds() const noexcept;

    // --- 低レベルパッキング ユーティリティ ---
    static constexpr uint64_t pack_word(uint64_t d0, uint64_t d1, uint64_t d2) noexcept {
        return (d0 & PART_MASK) | ((d1 & PART_MASK) << PART_BITS) | ((d2 & PART_MASK) << PART_BITS_TIMES_TWO);
    }

    static constexpr void unpack_word(uint64_t word, uint64_t& d0, uint64_t& d1, uint64_t& d2) noexcept {
        d0 = word & PART_MASK;
        d1 = (word >> PART_BITS) & PART_MASK;
        d2 = (word >> PART_BITS_TIMES_TWO) & PART_MASK;
    }

    // [NEW] 5進数桁インデックスから物理位置を O(1) で引くインラインヘルパー
    // これらを get_digit や set_digit の内部、あるいは外部から叩く
    static constexpr void resolve_index(std::size_t digit_index, std::size_t& word_index, unsigned& digit_bit_offset) noexcept {
        word_index = digit_index / DIGITS_PER_WORD;
        // 剰余演算の結果に直接 PART_BITS を掛けることで、中間変数を隠蔽する
        digit_bit_offset = static_cast<unsigned>(digit_index % DIGITS_PER_WORD) * PART_BITS;
    }

private:
    uint64_t* words_ = nullptr;
    std::size_t capacity_words_ = 0; // 確保しているメモリの量
    std::size_t active_words_ = 0;   // 現在入っている数字の大きさ（有効ワード数）
    bool is_negative_ = false;

    void update_active_words() noexcept {
        while (active_words_ > 0 && words_[active_words_ - 1] == 0) {
            --active_words_;
        }
    }

    // テスト関数（またはテスト用クラス）にだけ全アクセス権を開放
    friend void run_gmp_differential_sub_test(std::size_t iterations);
};