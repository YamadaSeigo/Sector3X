#pragma once

/**
 * @class NonCopyable
 * @brief コピーを禁止するためのクラスです。
 * @author suzuki
 * このクラスは、派生クラスのコピーと代入を防ぐために設計されています。
 * コピーコンストラクタとコピー代入演算子を削除しています。
 */
class NonCopyable {
public:
	/**
	 * @brief デフォルトコンストラクタ。
	 */
	NonCopyable() = default;

	/**
	 * @brief コピーコンストラクタを削除。
	 * @details このクラスのオブジェクトのコピーは許可されていないため、コピーコンストラクタは削除されています。
	 */
	NonCopyable(const NonCopyable&) = delete;

	/**
	 * @brief コピー代入演算子を削除。
	 * @details このクラスのオブジェクトの代入は許可されていないため、コピー代入演算子は削除されています。
	 */
	NonCopyable& operator=(const NonCopyable&) = delete;
};
