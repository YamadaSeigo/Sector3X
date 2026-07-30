// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Collision/PhysicsMaterialSimple.h>
#include <Jolt/Core/StreamUtils.h>

JPH_NAMESPACE_BEGIN

RefConst<Material> Material::sDefault;

JPH_IMPLEMENT_SERIALIZABLE_VIRTUAL(Material)
{
	JPH_ADD_BASE_CLASS(Material, SerializableObject)
}

void Material::SaveBinaryState(StreamOut& inStream) const
{
	inStream.Write(GetRTTI()->GetHash());
}

void Material::RestoreBinaryState(StreamIn& inStream)
{
	// RTTI hash is read in sRestoreFromBinaryState
}

Material::PhysicsMaterialResult Material::sRestoreFromBinaryState(StreamIn& inStream)
{
	return StreamUtils::RestoreObject<Material>(inStream, &Material::RestoreBinaryState);
}

JPH_NAMESPACE_END
