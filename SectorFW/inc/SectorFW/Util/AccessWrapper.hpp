/*****************************************************************//**
 * @file   AccessWrapper.hpp
 * @brief コンテナの読み書きビューを提供するクラス
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

 /**
  * @brief コンテナの読み書きビューを提供するクラス
  */
template<typename Container>
class ReadWriteView {
public:
	using iterator = typename Container::iterator;
	using const_iterator = typename Container::const_iterator;
	using value_type = typename Container::value_type;
	/**
	 * @brief コンストラクタ
	 * @param container コンテナの参照
	 * @return ReadWriteView コンテナの読み書きビュー
	 */
	explicit ReadWriteView(Container& container) noexcept : container_(container) {}
	/**
	 * @brief コンテナの先頭要素へのイテレータを取得
	 * @return iterator コンテナの先頭要素へのイテレータ
	 */
	iterator begin() noexcept { return container_.begin(); }
	/**
	 * @brief コンテナの末尾要素の次を指すイテレータを取得
	 * @return iterator コンテナの末尾要素の次を指すイテレータ
	 */
	iterator end() noexcept { return container_.end(); }
	/**
	 * @brief コンテナの先頭要素へのイテレータを取得（const）
	 * @return const_iterator コンテナの先頭要素へのイテレータ（const）
	 */
	const_iterator begin() const noexcept { return container_.begin(); }
	/**
	 * @brief コンテナの末尾要素の次を指すイテレータを取得（const）
	 * @return const_iterator コンテナの末尾要素の次を指すイテレータ（const）
	 */
	const_iterator end() const noexcept { return container_.end(); }
	/**
	 * @brief 指定したキーの要素を取得（at を持っているコンテナのみ有効）
	 * @param key キー
	 * @return 要素への参照
	 */
	template<typename Key>
	auto& at(const Key& key) {
		return container_.at(key);
	}

	// operator[] や insert, erase など構造変更は提供しない

private:
	/**
	 * @brief コンテナの参照
	 */
	Container& container_;
};