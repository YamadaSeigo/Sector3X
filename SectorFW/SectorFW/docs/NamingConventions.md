# Unreal Engine 命名規則ガイド（C++）

このドキュメントは、Unreal EngineプロジェクトでのC++コードにおける命名規則を統一し、可読性と保守性を高めることを目的とします。

---

## 📁 ファイル名

| 種類             | 命名規則          | 例                    |
|------------------|-------------------|-----------------------|
| ヘッダー         | クラス名と同じ    | `PlayerCharacter.h`   |
| ソース           | クラス名と同じ    | `PlayerCharacter.cpp` |
| インターフェース | `I` + クラス名    | `IInteractable.h`     |
| データ定義       | すべて小文字      | `component.h`         |

---

## 🏷 クラス名（PascalCase）

- 単語の区切りを大文字で始める（PascalCase）
- 接頭辞を使用（A, U, F, I など）

|          型          |  接頭辞  |         例          |
|----------------------|----------|---------------------|
| コンポーネント構造体 |   `C`    | `CEnemyBoss`        |
| システムクラス       |   `S`    | `SPlayerStats`      |
| インターフェース     |   `I`    | `IUsableObject`     |
| Enum                 |   `E`    | `ECharacterState`   |
| テンプレートクラス   |   `T`    | `TArray`            |

---

## 🔣 変数名（camelCase / スネークケース）

| 種類         | プレフィックス     | 規則          | 例                          |
|--------------|--------------------|---------------|-----------------------------|
| ローカル変数 | なし               | `camelCase`   | `playerHealth`              |
| メンバ変数   | `m_`               | `m_camelCase` | `m_isDead`                  |
| ポインタ     | `p` または `*`     | 任意（好み）  | `pOwner` / `*Owner`         |
| 参照         | `Ref`（任意）      | 読みやすく    | `targetRef`                 |
| コンスタント | `k` または全大文字 |               | `kMaxPlayers`, `MAX_HEALTH` |

---

## 🧩 関数名（PascalCase）

- 一般関数：`JumpToTarget()`
- Get/Set関数：`GetHealth()`, `SetSpeed()`
- デリゲート：`OnHealthChanged`

---

## 🔄 Enumの値（プレフィックス付き PascalCase）

```cpp
enum class EWeaponType
{
    EWT_Pistol,
    EWT_Rifle,
    EWT_Shotgun
};
