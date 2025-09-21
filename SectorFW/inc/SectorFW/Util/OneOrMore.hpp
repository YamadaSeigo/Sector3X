/*****************************************************************//**
 * @file   OneOrMore.hpp
 * @brief 1個以上の要素を格納するコンテナクラス
 * @author seigo_t03b63m
 * @date   September 2025
 *********************************************************************/

#pragma once
#include <variant>
#include <vector>
#include <optional>
#include <iterator>
#include <utility>
#include <type_traits>
#include <cassert>

 /**
  * @brief 1個以上の要素を格納するコンテナクラス
  */
template <class T>
class OneOrMore {
	std::variant<std::monostate, T, std::vector<T>> data_;

	/**
	 * @brief helper: 現在の先頭と終端ポインタを取り出す（分岐は一度だけ）
	 */
	static std::pair<const T*, const T*> ptr_range(const std::variant<std::monostate, T, std::vector<T>>& v) noexcept {
		if (std::holds_alternative<T>(v)) {
			const T& s = std::get<T>(v);
			return { std::addressof(s), std::addressof(s) + 1 };
		}
		if (std::holds_alternative<std::vector<T>>(v)) {
			const auto& vec = std::get<std::vector<T>>(v);
			return { vec.data(), vec.data() + vec.size() };
		}
		return { nullptr, nullptr };
	}

public:
	OneOrMore() = default;

	/**
	 * @brief 追加（コピー）
	 * @param value 追加する値
	 */
	void add(const T& value) {
		if (std::holds_alternative<std::monostate>(data_)) {
			data_ = value;
		}
		else if (std::holds_alternative<T>(data_)) {
			std::vector<T> vec;
			vec.reserve(2);                    // ★ single→multi 変換時の再確保を避ける
			vec.push_back(std::get<T>(data_));
			vec.push_back(value);
			data_ = std::move(vec);
		}
		else {
			std::get<std::vector<T>>(data_).push_back(value);
		}
	}
	/**
	 * @brief 追加（ムーブ）
	 * @param value 追加する値（右辺値参照）
	 */
	void add(T&& value) {
		if (std::holds_alternative<std::monostate>(data_)) {
			data_ = std::move(value);
		}
		else if (std::holds_alternative<T>(data_)) {
			std::vector<T> vec;
			vec.reserve(2);
			vec.push_back(std::move(std::get<T>(data_)));
			vec.push_back(std::move(value));
			data_ = std::move(vec);
		}
		else {
			std::get<std::vector<T>>(data_).push_back(std::move(value));
		}
	}
	/**
	 * @brief emplace（余計な一時を避ける）
	 * @param ...args コンストラクタ引数
	 * @return T& 追加された要素への参照
	 */
	template <class... Args>
	T& emplace(Args&&... args) {
		if (std::holds_alternative<std::monostate>(data_)) {
			data_.template emplace<T>(std::forward<Args>(args)...);
			return std::get<T>(data_);
		}
		else if (std::holds_alternative<T>(data_)) {
			std::vector<T> vec;
			vec.reserve(2);
			vec.push_back(std::move(std::get<T>(data_)));
			vec.emplace_back(std::forward<Args>(args)...);
			data_ = std::move(vec);
			return std::get<std::vector<T>>(data_).back();
		}
		else {
			auto& v = std::get<std::vector<T>>(data_);
			v.emplace_back(std::forward<Args>(args)...);
			return v.back();
		}
	}

	/**
	 * @brief サイズ / 空判定
	 */
	[[nodiscard]] size_t size() const noexcept {
		if (std::holds_alternative<std::monostate>(data_)) return 0;
		if (std::holds_alternative<T>(data_)) return 1;
		return std::get<std::vector<T>>(data_).size();
	}
	[[nodiscard]] bool empty() const noexcept { return size() == 0; }

	/**
	 * @brief 安全取得（optional<ref>）
	 * @param index 取得する要素のインデックス
	 * @return optional<ref> 取得できなければ nullopt
	 */
	[[nodiscard]] std::optional<std::reference_wrapper<const T>> get(size_t index) const {
		if (std::holds_alternative<T>(data_)) {
			if (index == 0) return std::cref(std::get<T>(data_));
		}
		else if (std::holds_alternative<std::vector<T>>(data_)) {
			const auto& vec = std::get<std::vector<T>>(data_);
			if (index < vec.size()) return std::cref(vec[index]);
		}
		return std::nullopt;
	}

	/**
	 * @brief 直接参照（高速だが自己責任）
	 * @param i インデックス
	 * @return const T& 参照
	 */
	const T& operator[](size_t i) const {
		if (std::holds_alternative<T>(data_)) { assert(i == 0); return std::get<T>(data_); }
		const auto& v = std::get<std::vector<T>>(data_);
		return v[i];
	}

	/**
	 * @brief reserve
	 */
	void reserve(size_t n) {
		if (n <= 1) return; // single で十分
		if (std::holds_alternative<std::vector<T>>(data_)) {
			std::get<std::vector<T>>(data_).reserve(n);
		}
		else if (std::holds_alternative<T>(data_)) {
			auto s = std::move(std::get<T>(data_));
			std::vector<T> v; v.reserve(n); v.push_back(std::move(s));
			data_ = std::move(v);
		}
		else {
			std::vector<T> v; v.reserve(n);
			data_ = std::move(v);
		}
	}
	/**
	 * @brief clear
	 */
	void clear() noexcept { data_.emplace<std::monostate>(); }

	/**
	 * @brief 反復：分岐なしの軽量イテレータ（ポインタ対）
	 */
	struct iterator {
		const T* cur = nullptr;
		const T* end = nullptr;
		using iterator_category = std::forward_iterator_tag;
		using value_type = T; using difference_type = std::ptrdiff_t; using pointer = const T*; using reference = const T&;
		reference operator*() const noexcept { return *cur; }
		pointer   operator->() const noexcept { return cur; }
		iterator& operator++() noexcept { ++cur; return *this; }
		bool operator==(const iterator& rhs) const noexcept { return cur == rhs.cur; }
		bool operator!=(const iterator& rhs) const noexcept { return cur != rhs.cur; }
	};
	/**
	 * @brief begin
	 * @return iterator 先頭イテレータ
	 */
	[[nodiscard]] iterator begin() const noexcept {
		auto [b, e] = ptr_range(data_);
		return { b, e };
	}
	/**
	 * @brief end
	 * @return iterator 終端イテレータ
	 */
	[[nodiscard]] iterator end() const noexcept {
		auto [b, e] = ptr_range(data_);
		(void)b; return { e, e };
	}
	/**
	 * @brief 高速 for_each（分岐1回で全要素処理）
	 */
	template <class F>
	void for_each(F&& f) const {
		if (std::holds_alternative<T>(data_)) {
			f(std::get<T>(data_));
		}
		else if (std::holds_alternative<std::vector<T>>(data_)) {
			const auto& v = std::get<std::vector<T>>(data_);
			for (const auto& x : v) f(x);
		}
	}
};

/**
 * @brief 1個から4個程度の要素を格納すると効率がいいコンテナクラス（Small Buffer Optimization版）
 */
template <class T, size_t SmallN = 4>
class OneOrMoreSBO {
	enum class Kind : unsigned char { Empty, Inline, Heap };
	Kind kind_ = Kind::Empty;
	size_t sz_ = 0;
	std::array<T, SmallN> inline_;
	std::vector<T> heap_;

public:
	OneOrMoreSBO() = default;

	/**
	 * @brief サイズ / 空判定
	 * @return size_t サイズ
	 */
	size_t size() const noexcept { return sz_; }
	/**
	 * @brief 空判定
	 * @return bool 空なら true
	 */
	bool   empty() const noexcept { return sz_ == 0; }
	/**
	 * @brief clear
	 */
	void clear() noexcept {
		if (kind_ == Kind::Heap) heap_.clear();
		kind_ = Kind::Empty; sz_ = 0;
	}
	/**
	 * @brief 追加（コピー）
	 * @param ...args コンストラクタ引数
	 * @return T& 追加された要素への参照
	 */
	template <class... Args>
	T& emplace(Args&&... args) {
		if (kind_ != Kind::Heap && sz_ < SmallN) {
			kind_ = Kind::Inline;
			T& r = inline_[sz_++];
			::new (static_cast<void*>(std::addressof(r))) T(std::forward<Args>(args)...);
			return r;
		}
		if (kind_ != Kind::Heap) {
			// migrate inline -> heap
			heap_.reserve(SmallN + 1);
			for (size_t i = 0; i < sz_; ++i) heap_.push_back(std::move(inline_[i]));
			kind_ = Kind::Heap;
		}
		heap_.emplace_back(std::forward<Args>(args)...);
		++sz_;
		return heap_.back();
	}
	/**
	 * @brief reserve
	 * @param n 予約する要素数
	 */
	void reserve(size_t n) {
		if (n <= SmallN) return;
		if (kind_ != Kind::Heap) {
			heap_.reserve(n);
			for (size_t i = 0; i < sz_; ++i) heap_.push_back(std::move(inline_[i]));
			kind_ = Kind::Heap;
		}
		else {
			heap_.reserve(n);
		}
	}
	/**
	 * @brief 直接参照（高速だが自己責任）
	 * @param i インデックス
	 * @return const T& 参照
	 */
	const T& operator[](size_t i) const {
		if (kind_ == Kind::Inline) { assert(i < sz_); return inline_[i]; }
		return heap_[i];
	}
	/**
	 * @brief 反復イテレータ（分岐なし
	 */
	struct iterator {
		const T* cur{}, * end{};
		const T& operator*() const noexcept { return *cur; }
		const T* operator->() const noexcept { return cur; }
		iterator& operator++() noexcept { ++cur; return *this; }
		bool operator==(const iterator& o) const noexcept { return cur == o.cur; }
		bool operator!=(const iterator& o) const noexcept { return cur != o.cur; }
	};
	/**
	 * @brief begin
	 * @return iterator 先頭イテレータ
	 */
	iterator begin() const noexcept {
		if (kind_ == Kind::Inline) return { inline_.data(), inline_.data() + sz_ };
		if (kind_ == Kind::Heap)   return { heap_.data(),   heap_.data() + heap_.size() };
		return { nullptr, nullptr };
	}
	/**
	 * @brief end
	 * @return iterator 終端イテレータ
	 */
	iterator end() const noexcept {
		if (kind_ == Kind::Inline) return { inline_.data() + sz_, inline_.data() + sz_ };
		if (kind_ == Kind::Heap)   return { heap_.data() + heap_.size(), heap_.data() + heap_.size() };
		return { nullptr, nullptr };
	}
};