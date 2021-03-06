#include "gitg-lane.h"

/* GitgLane functions */
GitgLane *
gitg_lane_copy(GitgLane *lane)
{
	GitgLane *copy = g_slice_new(GitgLane);
	copy->color = gitg_color_ref(lane->color);
	copy->from = g_slist_copy(lane->from);
	copy->type = lane->type;

	return copy;
}

GitgLane *
gitg_lane_dup(GitgLane *lane)
{
	GitgLane *dup = g_slice_new(GitgLane);
	dup->color = gitg_color_copy(lane->color);
	dup->from = g_slist_copy(lane->from);
	dup->type = lane->type;
	
	return dup;
}

void
gitg_lane_free(GitgLane *lane)
{
	gitg_color_unref(lane->color);
	g_slist_free(lane->from);
	
	if (GITG_IS_LANE_BOUNDARY(lane))
		g_slice_free(GitgLaneBoundary, (GitgLaneBoundary *)lane);
	else
		g_slice_free(GitgLane, lane);
}

GitgLane *
gitg_lane_new()
{
	return gitg_lane_new_with_color(NULL);
}

GitgLane *
gitg_lane_new_with_color(GitgColor *color)
{
	GitgLane *lane = g_slice_new0(GitgLane);
	lane->color = color ? gitg_color_ref(color) : gitg_color_next();
	
	return lane;
}

GitgLaneBoundary *
gitg_lane_convert_boundary(GitgLane *lane, GitgLaneType type)
{
	GitgLaneBoundary *boundary = g_slice_new(GitgLaneBoundary);
	
	boundary->lane = *lane;
	boundary->lane.type |= type;
	
	g_slice_free(GitgLane, lane);
	
	return boundary;
}
