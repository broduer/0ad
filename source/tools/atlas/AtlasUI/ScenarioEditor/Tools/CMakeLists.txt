target_sources(AtlasUI
	PRIVATE
		ActorViewerTool.cpp
		AlterElevation.cpp
		FillTerrain.cpp
		FlattenElevation.cpp
		PaintTerrain.cpp
		PickWaterHeight.cpp
		PikeElevation.cpp
		PlaceObject.cpp
		ReplaceTerrain.cpp
		SmoothElevation.cpp
		TransformObject.cpp
		TransformPath.cpp
)

add_subdirectory(Common/)
