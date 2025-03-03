#pragma once

//@file	    ファイル名を記載
//@brief	概要を記載
//@param	関数の引数で使用。
//@return / @retval	関数の戻り値で使用。複数の場合は retval を使用。
//@note	関数の補足説明に使用することができる。
//@author	プログラム著者を記載。
//@date	日付を記載。変更履歴など書くときに使用する。


/**
* @def _DEBUG
* @brief デバッグモード有効化
* @details 詳細な説明がある場合にはここに書く
*/
#define _DEBUG 1

/**
* @namespace 名前空間Sample
* @brief 名前空間の説明
* @details 詳細な説明がある場合はここに書く
*/
namespace Sample {

	/**
	* @brief クラスの説明
	* @details 詳細なクラスの説明
	*/
	class CSampleClass {
	public:
		/**
		* @brief コンストラクタの説明
		*/
		CSampleClass() {}
		/**
		* @brief デストラクタの説明
		*/
		~CSampleClass() {}

		/**
		* @fn int func
		* @brief 関数の簡単な関数
		* @details 詳細な説明がある場合にはここに書く
		*/
		int func();
	};

}