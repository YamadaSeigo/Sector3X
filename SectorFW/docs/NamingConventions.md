# 命名規則ガイド（C++）

このドキュメントは、C++コードにおける命名規則を統一し、可読性と保守性を高めることを目的とします。

---

## 📁 ファイル名

| 種類             | 命名規則          | 例                    |
|------------------|-------------------|-----------------------|
| ヘッダー          | クラス名と同じ    | `PlayerCharacter.h`   |
| ソース            | クラス名と同じ    | `PlayerCharacter.cpp` |
| インターフェース   | `I` + クラス名    | `IInteractable.h`     |
| テンプレートクラス | クラス名 + ".hpp" | `Level.hpp`     |
| データ定義        | すべて小文字      | `component.hpp`         |

---

## 🏷 クラス名（PascalCase）

- 単語の区切りを大文字で始める（PascalCase）
- 接頭辞を使用（A, U, F, I など）

|          型          |  接頭辞  |         例                                   |
|----------------------|----------|------------------------------------------|
| コンポーネント構造体    |   `C`    | `CEnemyBoss`                                |
| システムクラス         |   `S`    | `SPlayerStats` または'PlayerStateSystem'    |
| インターフェース       |   `I`    | `IUsableObject`                             |
| Enum                 |   `E`    | `ECharacterState` またはつけなくてもいい       |
| テンプレートクラス     |   `T`    | `TArray`      またはつけなくてもいい            |

---

## 🔣 変数名（camelCase / スネークケース）

| 種類         | プレフィックス     | 規則          | 例                          |
|-------------|--------------------|---------------|------------------------|
| ローカル変数  | なし              | `camelCase`  | `playerHealth`             |
| メンバ変数    | `m_`             | `m_camelCase`| `m_isDead`                 |
| ポインタ     | `p` または `*`     | 任意（好み）   | `pOwner` / `*Owner`        |
| 参照         | `Ref`（任意）      | 読みやすく    | `targetRef`                 |
| コンスタント  | `c` または全大文字  |              | `cMaxPlayers`, `MAX_HEALTH` |

---

## 🧩 関数名（PascalCase）

- 一般関数：`JumpToTarget()`
- Get/Set関数：`GetHealth()`, `SetSpeed()`
- フラグ関数 : IsFalling()
- 同期関数 : UpdateStateLock()
- デリゲート：`OnHealthChanged`

---

## 🔄 Enumの値（すべて大文字）

```cpp
enum class EWeaponType
{
    PISTOL,
    RIFLE,
    SHOTGUN,
    TYPE_MAX
};
