#include "Chunk.h"
#include "ChunkManager.h"
#include "glm/gtc/noise.hpp"
#include "glm/gtx/compatibility.hpp"

Chunk::Chunk(glm::ivec2 pos) : position_(pos), mesh_(World::chunkArea * 8), heightTimer_(0.0f), heightTimerIncreasing_(true), highestSolidBlock_(0)
{
}

void Chunk::Generate(TerrainGenerator &gen)
{
	glm::ivec3 chunk_pos = GetWorldPos();

	// Terrain
	for (int z = 0; z < World::chunkSize; z++)
	{
		for (int x = 0; x < World::chunkSize; x++)
		{
			glm::ivec2 pos = glm::ivec2(chunk_pos.x + x, chunk_pos.z + z);
			int height = gen.GetHeight(pos);

			for (int y = 0; y < height; y++)
			{
				Block block;

				if (y < height - 8)
					block = { Block::BLOCK_STONE };
				else if (y < height - 1)
					block = { Block::BLOCK_DIRT };
				else
					block = { Block::BLOCK_GRASS };

				SetBlockLocal({ x, y, z }, block);
			}
		}
	}
	
	// Trees
	glm::ivec3 treeSize = { _countof(*TerrainGenerator::tree), _countof(TerrainGenerator::tree), _countof(**TerrainGenerator::tree) };
	glm::ivec2 treeRad = { (treeSize.x - 1) / 2, (treeSize.z - 1) / 2 };
	glm::ivec2 chunkPos2d = { chunk_pos.x, chunk_pos.z };
	std::vector<glm::ivec2> treePoints = gen.GenerateTreePoints(chunkPos2d - treeRad, chunkPos2d + glm::ivec2(World::chunkSize, World::chunkSize) + treeRad);

	for (unsigned i = 0; i < treePoints.size(); i++)
	{
		for (int y = 0; y < treeSize.y; y++)
		{
			for (int x = -treeRad.x; x <= treeRad.x; x++)
			{
				for (int z = -treeRad.y; z <= treeRad.y; z++)
				{
					Block newBlock = { Block::BlockType(TerrainGenerator::tree[y][x + treeRad.x][z + treeRad.y]) };

					if (newBlock.type != Block::BLOCK_AIR)
					{
						glm::ivec3 blockPos = { treePoints[i].x + x, gen.GetHeight(treePoints[i]) + y, treePoints[i].y + z };

						const Block &oldBlock = GetBlock(blockPos);

						if (!(newBlock.type == Block::BLOCK_LEAVES && oldBlock.type != Block::BLOCK_AIR))
							SetBlock(blockPos, newBlock);
					}
				}
			}
		}
	}
}

void Chunk::BuildMesh()
{
	mesh_.Clear();

	for (int y = 0; y <= highestSolidBlock_; y++)
	{
		for (int z = 0; z < World::chunkSize; z++)
		{
			for (int x = 0; x < World::chunkSize; x++)
			{
				const Block &block = GetBlockLocal({ x, y, z });
				if (block.type == Block::BLOCK_AIR)
					continue;

				for (int d = 0; d < Math::DIRECTION_COUNT; d++)
				{
					glm::ivec3 adjacent = { x, y, z };
					glm::vec3 normal = Math::directionVectors[d];
					adjacent += normal;

					if (adjacent.y >= 0 && !CheckForBlock(adjacent))
					{
						const int tilesheetSize = 8;

						unsigned index = 0;
						
						switch (d)
						{
						case Math::DIRECTION_FORWARD:
						case Math::DIRECTION_BACKWARD:
						case Math::DIRECTION_LEFT:
						case Math::DIRECTION_RIGHT:
							index = BlockData::sideIndicies[block.type].side;
							break;
						case Math::DIRECTION_UP:
							index = BlockData::sideIndicies[block.type].top;
							break;
						case Math::DIRECTION_DOWN:
							index = BlockData::sideIndicies[block.type].bottom;
							break;
						}

						glm::vec2 offset = Math::GetUVFromSheet(tilesheetSize, tilesheetSize, index, Math::CORNER_TOP_LEFT);
						offset.y = 1.0f - offset.y - (1.0f / tilesheetSize); // ???

						unsigned char ambient[Math::CORNER_COUNT];

						for (int i = 0; i < Math::CORNER_COUNT; i++)
						{
							glm::ivec3 dir = Math::CornerToVec(Math::Corner(i), Math::Direction(d));
							glm::ivec3 corner = adjacent + dir;
							glm::ivec3 sides[2];

							unsigned current = 0;
							for (int j = 0; j < dir.length(); j++)
							{
								if (dir[j] == 0)
									continue;

								sides[current] = dir;
								sides[current][j] = 0;
								sides[current] += adjacent;
								current++;
							}
							bool cornerExists = CheckForBlock(corner);
							bool side0Exists = CheckForBlock(sides[0]);
							bool side1Exists = CheckForBlock(sides[1]);

							if (side0Exists && side1Exists)
								ambient[i] = 0;
							else
								ambient[i] = 3 - (int(side0Exists) + int(side1Exists) + int(cornerExists));
						}

						mesh_.AddQuad(
							Math::Direction(d),
							glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f) + normal * 0.5f, 1.0f / tilesheetSize, offset, ambient
						);
					}
				}
			}
		}
	}
	mesh_.TransferToGPU();
}

void Chunk::ClearMesh()
{
	mesh_.Clear();
	heightTimer_ = 0.0f;
}

bool Chunk::MeshBuilt() const
{
	return mesh_.IndexCount() != 0;
}

void Chunk::SetBlock(glm::ivec3 pos, const Block &block)
{
	glm::ivec3 local = WorldToLocal(pos);

	if (!OutOfBounds(local))
		SetBlockLocal(local, block);
}

const Block &Chunk::GetBlock(glm::ivec3 pos) const
{
	glm::ivec3 local = WorldToLocal(pos);

	if (OutOfBounds(local))
	{
		static Block error = { Block::BLOCK_ERROR };
		return error;
	}
	return GetBlockLocal(local);
}

glm::ivec2 Chunk::GetCoord() const
{
	return position_;
}

glm::vec3 Chunk::GetWorldPos() const
{
	return glm::vec3(position_.x * (float)World::chunkSize, 0, position_.y * (float)World::chunkSize);
}

glm::vec3 Chunk::GetRenderPos() const
{
	float t = 1.0f - heightTimer_;
	return GetWorldPos() - glm::vec3(0.0f, glm::lerp(0.0f, World::chunkFloatDistance, t * t * t), 0.0f);
}

void Chunk::UpdateHeightTimer(float dt)
{
	if (MeshBuilt())
	{
		if (heightTimerIncreasing_)
			heightTimer_ += World::chunkFloatInSpeed * dt;
		else
			heightTimer_ -= World::chunkFloatOutSpeed * dt;

		heightTimer_ = glm::clamp(heightTimer_, 0.0f, 1.0f);
	}
}

void Chunk::SetHeightTimerIncreasing(bool increasing)
{
	heightTimerIncreasing_ = increasing;
}

bool Chunk::HeightTimerHitZero() const
{
	return heightTimer_ == 0.0f && !heightTimerIncreasing_;
}

bool Chunk::IsVisible(const Math::Frustum &camera) const
{
	glm::vec3 position = GetRenderPos();

	for (unsigned i = 0; i < _countof(camera.planes); i++)
	{
		glm::vec3 positive = position;
		if (camera.planes[i].a >= 0)
			positive.x += World::chunkSize;
		if (camera.planes[i].b >= 0)
			positive.y += World::chunkHeight;
		if (camera.planes[i].c >= 0)
			positive.z += World::chunkSize;

		if (positive.x * camera.planes[i].a + positive.y * camera.planes[i].b + positive.z * camera.planes[i].c + camera.planes[i].d < 0)
			return false;
	}

	return true;
}

void Chunk::Draw()
{
	if (MeshBuilt())
		mesh_.Draw();
}

glm::ivec3 Chunk::WorldToLocal(glm::ivec3 pos) const
{
	glm::vec3 world = GetWorldPos();
	return glm::ivec3(pos.x - world.x, pos.y, pos.z - world.z);
}

glm::ivec3 Chunk::LocalToWorld(glm::ivec3 pos) const
{
	glm::vec3 world = GetWorldPos();
	return glm::ivec3(pos.x + world.x, pos.y, pos.z + world.z);
}

bool Chunk::OutOfBounds(glm::ivec3 pos) const
{
	return pos.x < 0 || pos.x >= World::chunkSize || pos.y < 0 || pos.y >= World::chunkHeight || pos.z < 0 || pos.z >= World::chunkSize;
}

const Block &Chunk::GetBlockLocal(glm::ivec3 pos) const
{
	return blocks_[pos.x + pos.y * World::chunkArea + pos.z * World::chunkSize];
}

void Chunk::SetBlockLocal(glm::ivec3 pos, const Block &block)
{
	blocks_[pos.x + pos.y * World::chunkArea + pos.z * World::chunkSize] = block;

	if (pos.y > highestSolidBlock_)
		highestSolidBlock_ = pos.y;
}

bool Chunk::CheckForBlock(glm::ivec3 pos) const
{
	if (!OutOfBounds(pos))
		return GetBlockLocal(pos).type != Block::BLOCK_AIR;
	else
	{
		if (pos.y < 0 || pos.y >= World::chunkHeight)
			return false;

		glm::ivec3 world = LocalToWorld(pos);
		Block block = ChunkManager::Instance().GetBlock(world);

		assert(block.type != Block::BLOCK_ERROR);

		return block.type != Block::BLOCK_AIR;
	}
}
