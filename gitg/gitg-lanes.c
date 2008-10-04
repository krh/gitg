#include "gitg-lanes.h"
#include "gitg-utils.h"

#define GITG_LANES_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE((object), GITG_TYPE_LANES, GitgLanesPrivate))

#define INACTIVE_MAX 4
#define INACTIVE_GAP 3

typedef struct
{
	GitgLane *lane;
	gint8 inactive;
	gchar const *hash;
} LaneContainer;

typedef struct 
{
	GitgColor *color;
	gint8 index;
} InactiveContainer;

struct _GitgLanesPrivate
{
	/* list of last N GitgRevisions used to backtrack in case of lane 
	   collapse/reactivation */
	GSList *backtrack;
	
	/* list of LaneContainer resembling the current lanes state for the 
	   next revision */
	GSList *lanes;
	
	/* hash table of rev hash -> InactiveLane where rev hash is the hash
	   to be expected on the lane */
	GHashTable *inactive;
};

G_DEFINE_TYPE(GitgLanes, gitg_lanes, G_TYPE_OBJECT)

static void
lane_container_free(LaneContainer *container)
{
	gitg_lane_free(container->lane);

	g_free(container);
}

static void
inactive_container_free(InactiveContainer *container)
{
	gitg_color_unref(container->color);

	g_free(container);
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

		if (container && container->hash && gitg_utils_hash_equal(container->hash, hash))
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
	g_hash_table_destroy(self->priv->inactive);
	
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
	
	self->priv->inactive = g_hash_table_new_full(gitg_utils_hash_hash,
												 gitg_utils_hash_equal, 
												 NULL, 
												 (GDestroyNotify)inactive_container_free);
}

GitgLanes *
gitg_lanes_new()
{
	return GITG_LANES(g_object_new(GITG_TYPE_LANES, NULL));
}

static LaneContainer *
lane_container_new(gchar const *hash)
{
	LaneContainer *ret = g_new(LaneContainer, 1);
	ret->hash = hash;
	ret->lane = gitg_lane_new();
	ret->inactive = 0;

	return ret;
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
	
	g_slist_foreach(lanes->priv->backtrack, (GFunc)g_object_unref, NULL);
	g_slist_free(lanes->priv->backtrack);
	lanes->priv->backtrack = NULL;
	
	g_hash_table_ref(lanes->priv->inactive);
	g_hash_table_destroy(lanes->priv->inactive);
}

static void 
update_inactive_lane_indices(GitgLane *lane, gint index)
{
	GSList *from;
		
	for (from = lane->from; from; from = from->next)
	{
		gint idx = GPOINTER_TO_INT(from->data);
		
		if (idx > index)
			from->data = GINT_TO_POINTER(idx - 1);
	}
}

/*static gchar *
from_to_list(GSList *from)
{
	GString *str = g_string_new("");
	GSList *item;
	
	for (item = from; item; item = item->next)
	{
		g_string_append_c(str, '0' + GPOINTER_TO_INT(item->data));
		g_string_append(str, ", ");
	}
	
	return str->str;
}

static void
debug_revision(GitgRevision *rv)
{
	g_message("DEBUG: %s, %d", gitg_revision_get_subject(rv), gitg_revision_get_mylane(rv));
	GSList *lns;
	GSList *item;
	gint i = 0;
	
	lns = gitg_revision_get_lanes(rv);
	
	for (item = lns; item; item = item->next)
	{
		g_message("DEBUG: lane %d, from: %s", i, from_to_list(((GitgLane *)item->data)->from));
		++i;
	}
	
	g_slist_free(lns);
}*/

static void
purge_inactive_lane(GitgLanes *lanes, LaneContainer *container, gint index)
{
	GSList *item;

	for (item = lanes->priv->lanes; item; item = item->next)
	{
		LaneContainer *container = (LaneContainer *)item->data;
		update_inactive_lane_indices(container->lane, index);
	}
	
	/* make sure to remove the lane from all the revisions in backtrack
	   and also update the lanes of each revision properly */
	for (item = lanes->priv->backtrack; item && item->next; item = item->next)
	{
		GitgRevision *rv = (GitgRevision *)item->data;
		GitgLane *purge;
		GSList *lns = gitg_revision_get_lanes(rv);
		GSList *ln;
		gint mylane = gitg_revision_get_mylane(rv);

		if (mylane > index)
			gitg_revision_set_mylane(rv, mylane - 1);
		
		/* track index */
		purge = (GitgLane *)g_slist_nth_data(lns, index);
		index = GPOINTER_TO_INT(purge->from->data);
		
		/* update indices */
		for (ln = lns; ln; ln = ln->next)
		{
			GitgLane *lane = (GitgLane *)ln->data;
			
			if (item->next->next)
				update_inactive_lane_indices(lane, index);
		
			if ((GitgLane *)ln->data != purge)
				ln->data = gitg_lane_copy((GitgLane *)ln->data);
		}
		
		/* remove lane */
		lns = g_slist_remove(lns, purge);
		
		/* set new lanes */
		gitg_revision_set_lanes(rv, lns);
	}
	
	if (item)
	{
		GitgRevision *rv = (GitgRevision *)item->data;
		GSList *lns = gitg_revision_get_lanes(rv);
		GitgLane *mylane = g_slist_nth_data(lns, index);
		
		mylane->type = GITG_LANE_TYPE_END;
		g_slist_free(lns);
	}
}

static void
purge_lanes(GitgLanes *lanes)
{
	GSList *copy;
	GSList *item;
	LaneContainer *container;
	gint8 index = 0;

	/* check every lane to see if it needs to be purged */
	copy = g_slist_copy(lanes->priv->lanes);
	for (item = copy; item; item = item->next)
	{
		container = (LaneContainer *)item->data;
		
		if (container->inactive == INACTIVE_MAX * 2 + INACTIVE_GAP)
		{
			purge_inactive_lane(lanes, container, index);
			lanes->priv->lanes = g_slist_remove(lanes->priv->lanes, container);
			lane_container_free(container);
		}
		else
		{
			++index;
		}
	}
	
	g_slist_free(copy);
}

static void
lane_container_next(GitgLanes *lanes, LaneContainer *container, gint index)
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
init_next_layer(GitgLanes *lanes)
{
	GSList *item;
	gint8 index = 0;
	
	// Initialize new set of lanes based on 'lanes'. It copies the lane (refs
	// the color) and adds the lane index as a merge (so it basicly represents
	// a passthrough)
	for (item = lanes->priv->lanes; item; item = item->next)
		lane_container_next(lanes, (LaneContainer *)item->data, index++);
}

static void
prepare_lanes(GitgLanes *lanes, GitgRevision *next, gint8 pos)
{
	LaneContainer *mylane;
	guint num;
	Hash *parents = gitg_revision_get_parents_hash(next, &num);
	guint i;
	
	/* prepare the next layer */
	init_next_layer(lanes);	
	mylane = (LaneContainer *)g_slist_nth_data(lanes->priv->lanes, pos);
	
	// Iterate over all parents and find them a lane
	for (i = 0; i < num; ++i)
	{
		gint8 lnpos;
		LaneContainer *container = find_lane_by_hash(lanes, parents[i], &lnpos);
		
		if (container)
		{
			// There already is a lane for this parent. This means that we add
			// mypos as a merge for the lane, also this means the color of 
			// this lane incluis the merge should change to one color
			container->lane->from = g_slist_append(container->lane->from, GINT_TO_POINTER((gint)pos));
			gitg_color_next_index(container->lane->color);
			container->inactive = 0;
			
			continue;
		} 
		else if (mylane && mylane->hash == NULL)
		{
			// There is no parent yet which can proceed on the current
			// revision lane, so set it now
			mylane->hash = (gchar const *)parents[i];
	
			// If there is more than one parent, then also change the color 
			// since this revision is a merge
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
			// Generate a new lane for this parent
			LaneContainer *newlane = lane_container_new(parents[i]);
			newlane->lane->from = g_slist_prepend(NULL, GINT_TO_POINTER((gint)pos));
			lanes->priv->lanes = g_slist_append(lanes->priv->lanes, newlane);
		}
	}
	
	// Remove the current lane if it is no longer needed
	if (mylane && mylane->hash == NULL)
	{
		lanes->priv->lanes = g_slist_remove(lanes->priv->lanes, mylane);
		lane_container_free (mylane);
	}
}

GSList *
gitg_lanes_next(GitgLanes *lanes, GitgRevision *next, gint8 *nextpos)
{
	purge_lanes(lanes);

	LaneContainer *mylane = find_lane_by_hash(lanes, gitg_revision_get_hash(next), nextpos);
	GSList *res;
	
	if (!mylane)
	{
		/* apparently, there is no lane reserved for this revision, we
		   add a new one */
		lanes->priv->lanes = g_slist_append(lanes->priv->lanes, lane_container_new(NULL));
		*nextpos = g_slist_length(lanes->priv->lanes) - 1;
	}
	else
	{
		/* copy the color here because this represents a new stop */
		GitgColor *nc = gitg_color_copy(mylane->lane->color);
		gitg_color_unref(mylane->lane->color);

		mylane->lane->color = nc;
		mylane->hash = NULL;
		mylane->inactive = 0;
	}
	
	/* update backtrack list */
	while (g_slist_length(lanes->priv->backtrack) > INACTIVE_GAP + INACTIVE_MAX)
	{
		GSList *last = g_slist_last(lanes->priv->backtrack);
		
		g_object_unref(last->data);
		lanes->priv->backtrack = g_slist_delete_link(lanes->priv->backtrack, 
													 last);
	}

	lanes->priv->backtrack = g_slist_prepend(lanes->priv->backtrack, g_object_ref(next));
	
	res = lanes_list(lanes);
	prepare_lanes(lanes, next, *nextpos);

	return res;
}
