/*****************************************************************//**
 * @file   alignment.h
 * @brief アライメントを計算するヘッダーファイル
 * @author seigo_t03b63m
 * @date   June 2025
 *********************************************************************/

#pragma once

 /**
  * @brief 指定したオフセットをアライメントに合わせて調整する関数
  * @param offset オフセット値
  * @param alignment アライメント値
  * @return 調整後のオフセット値
  */
inline size_t AlignTo(size_t offset, size_t alignment) {
	return (offset + alignment - 1) & ~(alignment - 1);
}