target_sources(ActorEditor
	PRIVATE
		$<$<PLATFORM_ID:Windows>: ActorEditor.rc> # Caution, the blank before the File is needed
		ActorEditor.cpp
)
