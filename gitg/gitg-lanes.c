#include "gitg-lanes.h"
#include "gitg-utils.h"
#include <string.h>

#define GITG_LANES_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE((object), GITG_TYPE_LANES, GitgLanesPrivate))

#define INACTIVE_MAX 30
#define INACTIVE_COLLAPSE 10
#define INACTIVE_GAP 10

typedef struct
{
	GitgLane *lane;
	guint8 inactive;
	gchar const *from;
	gchar const *to;
} LaneContainer;

typedef struct 
{
	GitgColor *color;
	gint8 index;
	gchar const *from;
	gchar const *to;
} CollapsedLane;

struct _GitgLanesPrivate
{
	/* list of last N GitgRevisions used to backtrack in case of lane 
	   collapse/reactivation */
	GSList *previous;
	
	/* list of LaneContainer resembling the current lanes state for the 
	   next revision */
	GSList *lanes;
	
	/* hash table of rev hash -> CollapsedLane where rev hash is the hash
	   to be expected on the lane */
	GHashTable *collapsed;
};

G_DEFINE_TYPE(GitgLanes, gitg_lanes, G_TYPE_OBJECT)

static void
lane_container_free(LaneContainer *container)
{
	gitg_lane_free(container->lane);
	g_slice_free(LaneContainer, container);
}

static void
collapsed_lane_free(CollapsedLane *lane)
{
	gitg_color_unref(lane->color);
	g_slice_free(CollapsedLane, lane);
}

static CollapsedLane *
collapsed_lane_new(LaneContainer *container)
{
	CollapsedLane *collapsed = g_slice_new(CollapsedLane);
	collapsed->color = gitg_color_ref(container->lane->color);
	collapsed->from = container->from;
	collapsed->to = container->to;
	
	return collapsed;
}

static void
free_lanes(GitgLanes *lanes)
{
	g_slist_foreach(lanes->priv->lanes, (GFunc)lane_container_free, NULL);
	g_slist_free(lanes->priv->lanes);
	
	lanes->priv->lanes = NULL;
}

static LaneContainer *
find_lane_by_hash(GitgLanes *lanes, gchar const *hash, gint8 *pos)
{
	GSList *item;
	gint8 p = 0;

	if (!hash)
		return NULL;

	for (item = lanes->priv->lanes; item; item = item->next)
	{
		LaneContainer *container = (LaneContainer *)(item->data);

		if (container && container->to && gitg_utils_hash_equal(container->to, hash))
		{
			if (pos)
				*pos = p;
		
			return container;
		}

		++p;
	}
	
	return NULL;
}

/* GitgLanes functions */
static void
gitg_lanes_finalize(GObject *object)
{
	GitgLanes *self = GITG_LANES(object);
	
	gitg_lanes_reset(self);
	g_hash_table_destroy(self->priv->collapsed);
	
	G_OBJECT_CLASS(gitg_lanes_parent_class)->finalize(object);
}

static void
gitg_lanes_class_init(GitgLanesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	
	object_class->finalize = gitg_lanes_finalize;
	
	g_type_class_add_private(object_class, sizeof(GitgLanesPrivate));
}

static void
gitg_lanes_init(GitgLanes *self)
{
	self->priv = GITG_LANES_GET_PRIVATE(self);
	self->priv->collapsed = g_hash_table_new_full(gitg_utils_hash_hash, gitg_utils_hash_equal, NULL, (GDestroyNotify)collapsed_lane_free);
}

GitgLanes *
gitg_lanes_new()
{
	return GITG_LANES(g_object_new(GITG_TYPE_LANES, NULL));
}

static LaneContainer *
lane_container_new_with_color(gchar const *from, gchar const *to, GitgColor *color)
{
	LaneContainer *ret = g_slice_new(LaneContainer);

	ret->from = from;
	ret->to = to;
	ret->lane = gitg_lane_new_with_color(color);
	ret->inactive = 0;

	return ret;
}

static LaneContainer *
lane_container_new(gchar const *from, gchar const *to)
{
	return lane_container_new_with_color(from, to, NULL);
}

GSList *
lanes_list(GitgLanes *lanes)
{
	GSList *lns = NULL;
	GSList *item;
	
	for (item = lanes->priv->lanes; item; item = item->next)
		lns = g_slist_prepend(lns, gitg_lane_copy(((LaneContainer*)item->data)->lane));
	
	return g_slist_reverse(lns);
}

void
gitg_lanes_reset(GitgLanes *lanes)
{
	free_lanes(lanes);
	gitg_color_reset();
	
	g_slist_foreach(lanes->priv->previous, (GFunc)gitg_revision_unref, NULL);
	g_slist_free(lanes->priv->previous);
	lanes->priv->previous = NULL;
	
	g_hash_table_remove_all(lanes->priv->collapsed);
}

static gboolean
lane_container_next(LaneContainer *container, gint index)
{
	GitgLane *lane = gitg_lane_copy(container->lane);
	lane->type = GITG_LANE_TYPE_NONE;
	g_slist_free(lane->from);
	
	gitg_lane_free(container->lane);

	container->lane = lane;
	container->lane->from = g_slist_prepend(NULL, GINT_TO_POINTER((gint)(index)));
	
	++container->inactive;
}

static void
update_lane_merge_indices(GSList *from, gint8 index, gint direction)
{
	GSList *item;
	
	for (item = from; item; item = g_slist_next(item))
	{
		gint8 idx = GPOINTER_TO_INT(item->data);

		if ((direction < 0 && idx > index) || (direction > 0 && idx >= index))
			item->data = GINT_TO_POINTER(idx + direction);
	}
}

static void
update_merge_indices(GSList *lanes, gint8 index, gint direction)
{
	GSList *item;
	
	for (item = lanes; item; item = g_slist_next(item))
	{
		GitgLane *lane = (GitgLane *)item->data;
		
		update_lane_merge_indices(lane->from, index, direction);
	}
}

static void
add_collapsed(GitgLanes *lanes, LaneContainer *container, gint8 index)
{
	CollapsedLane *collapsed = collapsed_lane_new(container);
	collapsed->index = index;
	
	g_hash_table_insert(lanes->priv->collapsed, (gpointer)container->to, collapsed);
}

static void
collapse_lane(GitgLanes *lanes, LaneContainer *container, gint8 index)
{
	/* backtrack for INACTIVE_COLLAPSE revisions and remove this container from
	   those revisions, appropriately updating merge indices etc */
	GSList *item;
	
	add_collapsed(lanes, container, index);
	
	for (item = lanes->priv->previous; item; item = g_slist_next(item))
	{
		GitgRevision *revision = GITG_REVISION(item->data);
		GSList *lns = gitg_revision_get_lanes(revision);
		gint8 newindex = index;
		
		/* remove lane at 'index' and update merge indices for the lanes
		   after 'index' in the list */
		if (item->next)
		{
			GSList *collapsed = g_slist_nth(lns, index);
			GitgLane *lane = (GitgLane *)collapsed->data;
			
			gint8 newindex = GPOINTER_TO_INT(lane->from->data);

			lns = gitg_revision_remove_lane(revision, lane);
			
			if (item->next->next)
				update_merge_indices(lns, newindex, -1);
			
			gint mylane = gitg_revision_get_mylane(revision);
			
			if (mylane > index)
				gitg_revision_set_mylane(revision, mylane - 1);
	
			index = newindex;
		}
		else
		{
			/* the last item we keep, and set the style of the lane to END */
			GSList *lstlane = g_slist_nth(lns, index);
			GitgLaneBoundary *boundary = gitg_lane_convert_boundary((GitgLane *)lstlane->data, GITG_LANE_TYPE_END);
			
			/* copy parent hash over */
			memcpy(boundary->hash, container->to, 20);
			lstlane->data = boundary;
		}
	}	
}

static void
update_current_lanes_merge_indices(GitgLanes *lanes, gint8 index, gint8 direction)
{
	GSList *item;
	
	for (item = lanes->priv->lanes; item; item = g_slist_next(item))
		update_lane_merge_indices(((LaneContainer *)item->data)->lane->from, index, direction);
}

static void
collapse_lanes(GitgLanes *lanes)
{
	GSList *item = lanes->priv->lanes;
	gint8 index = 0;

	while (item)
	{
		LaneContainer *container = (LaneContainer *)item->data;
		
		if (container->inactive != INACTIVE_MAX + INACTIVE_GAP)
		{
			item = g_slist_next(item);
			++index;
			continue;
		}

		collapse_lane(lanes, container, GPOINTER_TO_INT(container->lane->from->data));
		update_current_lanes_merge_indices(lanes, index, -1);
		
		GSList *next = g_slist_next(item);

		lane_container_free(container);

		lanes->priv->lanes = g_slist_remove_link(lanes->priv->lanes, item);
		item = next;
	}
}

static gint8
ensure_correct_index(GitgRevision *revision, gint8 index)
{
	guint len = g_slist_length(gitg_revision_get_lanes(revision));
	
	if (index > len)
		index = len;
	
	return index;
}

static void
expand_lane(GitgLanes *lanes, CollapsedLane *lane)
{
	GSList *item;
	gint8 index = lane->index;

	GitgLane *ln = gitg_lane_new_with_color(lane->color);
	guint len = g_slist_length(lanes->priv->lanes);
	gint8 next;
	
	if (index > len)
		index = len;

	next = ensure_correct_index((GitgRevision *)lanes->priv->previous->data, index);
	LaneContainer *container = lane_container_new_with_color(lane->from, lane->to, lane->color);

	update_current_lanes_merge_indices(lanes, index, 1);

	container->lane->from = g_slist_prepend(NULL, GINT_TO_POINTER((gint)next));
	lanes->priv->lanes = g_slist_insert(lanes->priv->lanes, container, index);

	index = next;
	guint cnt = 0;
	
	for (item = lanes->priv->previous; item; item = g_slist_next(item))
	{
		GitgRevision *revision = GITG_REVISION(item->data);

		if (cnt == INACTIVE_COLLAPSE)
			break;

		/* insert new lane at the index */
		GitgLane *copy = gitg_lane_copy(ln);
		GSList *lns = gitg_revision_get_lanes(revision);

		if (!item->next || cnt + 1 == INACTIVE_COLLAPSE)
		{
			GitgLaneBoundary *boundary = gitg_lane_convert_boundary(copy, GITG_LANE_TYPE_START);
			
			/* copy child hash in boundary */
			memcpy(boundary->hash, lane->from, 20);
			copy = (GitgLane *)boundary;
		}
		else
		{
			next = ensure_correct_index(GITG_REVISION(item->next->data), index);
			copy->from = g_slist_prepend(NULL, GINT_TO_POINTER((gint)next));
			
			/* update merge indices */
			update_merge_indices(lns, index, 1);
		}

		lns = gitg_revision_insert_lane(revision, copy, index);
		gint mylane = gitg_revision_get_mylane(revision);
		
		if (mylane >= index)
			gitg_revision_set_mylane(revision, mylane + 1);
		
		index = next;
		++cnt;
	}
	
	gitg_lane_free(ln);
}

static void
expand_lane_from_hash(GitgLanes *lanes, gchar const *hash)
{
	CollapsedLane *collapsed = (CollapsedLane *)g_hash_table_lookup(lanes->priv->collapsed, hash);

	if (!collapsed)
		return;
	
	expand_lane(lanes, collapsed);
	g_hash_table_remove(lanes->priv->collapsed, hash);
}

static void
expand_lanes(GitgLanes *lanes, GitgRevision *revision)
{
	/* expand any lanes that revision needs (own lane and parents lanes) */
	expand_lane_from_hash(lanes, gitg_revision_get_hash(revision));
	
	guint num;
	guint i;
	Hash *parents = gitg_revision_get_parents_hash(revision, &num);

	for (i = 0; i < num; ++i)
		expand_lane_from_hash(lanes, parents[i]);
}

static void
init_next_layer(GitgLanes *lanes)
{
	GSList *item = lanes->priv->lanes;
	gint8 index = 0;
	
	/* Initialize new set of lanes based on 'lanes'. It copies the lane (refs
	   the color) and adds the lane index as a merge (so it basicly represents
	   a passthrough) */
	for (item = lanes->priv->lanes; item; item = g_slist_next(item))
	{
		LaneContainer *container = (LaneContainer *)item->data;

		lane_container_next(container, index++);
	}
}

static void
prepare_lanes(GitgLanes *lanes, GitgRevision *next, gint8 *pos)
{
	LaneContainer *mylane;
	guint num;
	Hash *parents = gitg_revision_get_parents_hash(next, &num);
	guint i;
	gchar const *myhash = gitg_revision_get_hash(next);
	
	/* prepare the next layer */
	init_next_layer(lanes);
	
	mylane = (LaneContainer *)g_slist_nth_data(lanes->priv->lanes, *pos);
	
	/* Iterate over all parents and find them a lane */
	for (i = 0; i < num; ++i)
	{
		gint8 lnpos;
		LaneContainer *container = find_lane_by_hash(lanes, parents[i], &lnpos);
		
		if (container)
		{
			/* There already is a lane for this parent. This means that we add
			   mypos as a merge for the lane, also this means the color of 
			   this lane incluis the merge should change to one color */
			container->lane->from = g_slist_append(container->lane->from, GINT_TO_POINTER((gint)*pos));
			gitg_color_next_index(container->lane->color);
			container->inactive = 0;
			container->from = gitg_revision_get_hash(next);
			
			continue;
		} 
		else if (mylane && mylane->to == NULL)
		{
			/* There is no parent yet which can proceed on the current
			   revision lane, so set it now */
			mylane->to = (gchar const *)parents[i];
			
			/* If there is more than one parent, then also change the color 
			   since this revision is a merge */
			if (num > 1)
			{
				gitg_color_unref(mylane->lane->color);
				mylane->lane->color = gitg_color_next();
			}
			else
			{
				GitgColor *nc = gitg_color_copy(mylane->lane->color);
				gitg_color_unref(mylane->lane->color);
				mylane->lane->color = nc;
			}
		}
		else
		{
			/* Generate a new lane for this parent */
			LaneContainer *newlane = lane_container_new(myhash, parents[i]);
			newlane->lane->from = g_slist_prepend(NULL, GINT_TO_POINTER((gint)*pos));
			lanes->priv->lanes = g_slist_append(lanes->priv->lanes, newlane);
		}
	}
	
	/* Remove the current lane if it is no longer needed */
	if (mylane && mylane->to == NULL)
	{
		lanes->priv->lanes = g_slist_remove(lanes->priv->lanes, mylane);
	}

	/* Store new revision in our track list */
	if (g_slist_length(lanes->priv->previous) == INACTIVE_COLLAPSE + INACTIVE_GAP + 1)
	{
		GSList *last = g_slist_last(lanes->priv->previous);
		gitg_revision_unref(GITG_REVISION(last->data));
		
		lanes->priv->previous = g_slist_remove_link(lanes->priv->previous, last);
	}

	lanes->priv->previous = g_slist_prepend(lanes->priv->previous, gitg_revision_ref(next));
}

GSList *
gitg_lanes_next(GitgLanes *lanes, GitgRevision *next, gint8 *nextpos)
{
	LaneContainer *mylane;
	GSList *res;
	gchar const *myhash = gitg_revision_get_hash(next);

	collapse_lanes(lanes);
	expand_lanes(lanes, next);

	mylane = find_lane_by_hash(lanes, myhash, nextpos);

	if (!mylane)
	{
		/* apparently, there is no lane reserved for this revision, we
		   add a new one */
		lanes->priv->lanes = g_slist_append(lanes->priv->lanes, lane_container_new(myhash, NULL));
		*nextpos = g_slist_length(lanes->priv->lanes) - 1;
	}
	else
	{
		/* copy the color here because this represents a new stop */
		GitgColor *nc = gitg_color_copy(mylane->lane->color);
		gitg_color_unref(mylane->lane->color);

		mylane->lane->color = nc;
		mylane->to = NULL;
		mylane->from = gitg_revision_get_hash(next);
		mylane->inactive = 0;
	}

	res = lanes_list(lanes);
	prepare_lanes(lanes, next, nextpos);

	return res;
}
