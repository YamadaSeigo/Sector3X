/*****************************************************************//**
 * @file   OneOrMore.hpp
 * @brief 一つか複数の要素を保持するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

#include <variant>
#include <vector>
#include <iterator>
#include <stdexcept>

/**
 * @brief 一つか複数の要素を保持するクラス
 * @tparam T 要素の型
 * @details このクラスは、要素が一つだけの場合と複数ある場合の両方を効率的に扱うことができます。
 */
template<typename T>
class OneOrMore {
    std::variant<std::monostate, T, std::vector<T>> data_;

public:
    /**
	 * @brief デフォルトコンストラクタ
     */
    OneOrMore() = default;
    /**
	 * @brief 要素の追加
	 * @param value 追加する要素
     */
    void add(const T& value) {
        if (std::holds_alternative<std::monostate>(data_)) {
            data_ = value;
        }
        else if (std::holds_alternative<T>(data_)) {
            std::vector<T> vec;
            vec.push_back(std::get<T>(data_));
            vec.push_back(value);
            data_ = std::move(vec);
        }
        else {
            std::get<std::vector<T>>(data_).push_back(value);
        }
    }
    /**
     * @brief 要素の数を取得
	 * @return 要素の数
     */
    size_t size() const noexcept {
        if (std::holds_alternative<std::monostate>(data_)) return 0;
        if (std::holds_alternative<T>(data_)) return 1;
        return std::get<std::vector<T>>(data_).size();
    }
    /**
     * @brief 要素を取得
	 * @param index インデックス
	 * @return 指定されたインデックスの要素
     */
    const T& get(size_t index) const noexcept {
        if (std::holds_alternative<T>(data_)) {
            if (index == 0) return std::get<T>(data_);
        }
        else if (std::holds_alternative<std::vector<T>>(data_)) {
            return std::get<std::vector<T>>(data_)[index];
        }
        throw std::out_of_range("Invalid index");
    }
    /**
	 * @brief 追加される要素のためにメモリを予約
	 * @param n　予約する要素数
     */
    void reserve(size_t n) {
        if (std::holds_alternative<std::monostate>(data_)) {
            data_ = std::vector<T>();
            std::get<std::vector<T>>(data_).reserve(n);
        }
        else if (std::holds_alternative<T>(data_)) {
            std::vector<T> vec;
            vec.reserve((std::max)(n, size_t(2))); // 元の1要素+将来の追加を考慮
            vec.push_back(std::get<T>(data_));
            data_ = std::move(vec);
        }
        else {
            std::get<std::vector<T>>(data_).reserve(n);
        }
    }
    /**
	 * @brief 要素をリサイズ
	 * @param n 新しいサイズ
	 * @param value リサイズ時に使用する値（デフォルトはTのデフォルトコンストラクタ）
     */
    void resize(size_t n, const T& value = T{}) {
        if (n == 0) {
            data_ = std::monostate{};
        }
        else if (n == 1) {
            data_ = value;
        }
        else {
            std::vector<T> vec(n, value);
            data_ = std::move(vec);
        }
    }
    /**
     * @brief イテレータクラス
	 * @details このクラスは、OneOrMoreの要素をイテレートするためのクラスです。
	 * @tparam T 要素の型
     */
    struct iterator {
        using IterType = typename std::vector<T>::const_iterator;
        enum class Mode { Empty, Single, Multi };

    private:
        Mode mode = Mode::Empty;
        T const* single = nullptr;
        IterType begin_, end_;
    public:
        /**
		 * @brief デフォルトコンストラクタ
         */
        iterator() = default;
        /**
		 * @brief シングル要素のイテレータを初期化
		 * @param ptr シングル要素のポインタ
         */
        iterator(const T* ptr) : mode(Mode::Single), single(ptr) {}
        /**
		 * @brief 複数要素のイテレータを初期化
		 * @param b 開始イテレータ
		 * @param e 終了イテレータ
         */
        iterator(IterType b, IterType e) : mode(Mode::Multi), begin_(b), end_(e) {}
        /**
		 * @brief イテレータのデリファレンス
		 * @return 現在の要素の参照
         */
        const T& operator*() const noexcept {
            if (mode == Mode::Single) return *single;
            return *begin_;
        }
        /**
		 * @brief イテレータのポインタ演算子
		 * @return 現在の要素のポインタ
         */
		const T& operator->() const noexcept {
			if (mode == Mode::Single) return *single;
			return *begin_;
		}
        /**
		 * @brief イテレータのインクリメント
		 * @return 自身の参照
         */
        iterator& operator++() noexcept {
            if (mode == Mode::Single) {
                mode = Mode::Empty;
            }
            else if (mode == Mode::Multi) {
                ++begin_;
                if (begin_ == end_) mode = Mode::Empty;
            }
            return *this;
        }
        /**
		 * @brief イテレータの比較
		 * @param other 比較対象のイテレータ
		 * @return 比較結果（等しい場合はtrue、異なる場合はfalse）
         */
        bool operator==(const iterator& other) const noexcept {
            return !(*this != other);
        }
        /**
		 * @brief イテレータの比較
		 * @param other 比較対象のイテレータ
		 * @return 比較結果（等しい場合はfalse、異なる場合はtrue）
         */
        bool operator!=(const iterator& other) const noexcept {
            if (mode != other.mode) return true;
            if (mode == Mode::Empty) return false;
            if (mode == Mode::Single) return single != other.single;
            return begin_ != other.begin_;
        }
    };
    /**
	 * @brief イテレータの取得
	 * @return イテレータの開始位置
     */
    iterator begin() const noexcept {
        if (std::holds_alternative<T>(data_)) {
            return iterator(&std::get<T>(data_));
        }
        else if (std::holds_alternative<std::vector<T>>(data_)) {
            const auto& vec = std::get<std::vector<T>>(data_);
            return iterator(vec.begin(), vec.end());
        }
        else {
            return iterator();
        }
    }
    /**
	 * @brief イテレータの取得
	 * @return イテレータの終了位置
     */
    iterator end() const noexcept {
        if (std::holds_alternative<T>(data_)) {
            return iterator(); // mode = Empty, which acts as end for Single
        }
        else if (std::holds_alternative<std::vector<T>>(data_)) {
            const auto& vec = std::get<std::vector<T>>(data_);
            return iterator(vec.end(), vec.end());
        }
        else {
            return iterator(); // Empty
        }
    }
};
