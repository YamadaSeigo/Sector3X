#pragma once

template<typename Container>
class ReadWriteView {
public:
	using iterator = typename Container::iterator;
	using const_iterator = typename Container::const_iterator;
	using value_type = typename Container::value_type;

	ReadWriteView(Container& container) : container_(container) {}

	// 値のアクセス
	iterator begin() { return container_.begin(); }
	iterator end() { return container_.end(); }

	// 読み取り用 const も許可（任意）
	const_iterator begin() const { return container_.begin(); }
	const_iterator end() const { return container_.end(); }

	// 要素アクセス（at を持っているコンテナのみ有効）
	template<typename Key>
	auto& at(const Key& key) {
		return container_.at(key);
	}

	// operator[] や insert, erase など構造変更は提供しない

private:
	Container& container_;
};
