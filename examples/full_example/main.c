/* Copyright 2007 Openismus GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <clutter/clutter.h>
#include <stdlib.h>

ClutterActor *stage = NULL;

/* For showing the filename: */
ClutterActor *label_filename = NULL;

/* For rotating all images around an ellipse: */
ClutterTimeline *timeline_rotation = NULL;

/* For moving one image up and scaling it: */
ClutterTimeline *timeline_moveup = NULL;
ClutterBehaviour *behaviour_scale = NULL;
ClutterBehaviour *behaviour_path = NULL;
ClutterBehaviour *behaviour_opacity = NULL;

/* The y position of the ellipse of images: */
const gint ELLIPSE_Y = 390;
const gint ELLIPSE_HEIGHT = 450; /* The distance from front to back when it's rotated 90 degrees. */
const gint IMAGE_HEIGHT = 100;

static gboolean
on_texture_button_press (ClutterActor *actor, ClutterEvent *event, gpointer data);

const double angle_step = 30;

typedef struct Item
{
  ClutterActor *actor;
  ClutterBehaviour *ellipse_behaviour;
  gchar* filepath;
}
Item;

Item* item_at_front = NULL;

GSList *list_items = 0;

void on_foreach_clear_list_items(gpointer data, gpointer user_data G_GNUC_UNUSED)
{
  Item* item = (Item*)data;

  /* We don't need to unref the actor because the floating reference was taken by the stage. */
  g_object_unref (item->ellipse_behaviour);
  g_free (item->filepath);
  g_free (item);
}

void scale_texture_default(ClutterActor *texture)
{
  int pixbuf_height = 0;
  clutter_texture_get_base_size (CLUTTER_TEXTURE (texture), NULL, &pixbuf_height);

  const gdouble scale = pixbuf_height ? IMAGE_HEIGHT /  (gdouble)pixbuf_height : 0;
  clutter_actor_set_scale (texture, scale, scale);
}

void load_images(const gchar* directory_path)
{
  g_return_if_fail(directory_path);

  /* Clear any existing images: */
  g_slist_foreach (list_items, on_foreach_clear_list_items, NULL);
  g_slist_free (list_items);

  /* Create a new list: */
  list_items = NULL;
  
  /* Discover the images in the directory: */
  GError *error = NULL;
  GDir* dir = g_dir_open (directory_path, 0, &error);
  if(error)
  {
    g_warning("g_dir_open() failed: %s\n", error->message);
    g_clear_error(&error);
    return;
  }

  const gchar* filename = NULL;
  while ( (filename = g_dir_read_name(dir)) )
  {
    gchar* path = g_build_filename (directory_path, filename, NULL);

    /* Try to load the file as an image: */
    ClutterActor *actor = clutter_texture_new_from_file (path, NULL);
    if(actor)
    {
      Item* item = g_new0(Item, 1);

      item->actor = actor;
      item->filepath = g_strdup(path);

      /* Make sure that all images are shown with the same height: */
      scale_texture_default (item->actor);

      list_items = g_slist_append (list_items, item);
    }

    g_free (path);
  }
}


void add_to_ellipse_behaviour(ClutterTimeline *timeline_rotation, gdouble start_angle, Item *item)
{
  g_return_if_fail (timeline_rotation);

  ClutterAlpha *alpha = clutter_alpha_new_full (timeline_rotation, CLUTTER_EASE_OUT_SINE);
 
  item->ellipse_behaviour = clutter_behaviour_ellipse_new (alpha, 
    320, ELLIPSE_Y, /* x, y */
    ELLIPSE_HEIGHT, ELLIPSE_HEIGHT, /* width, height */
    CLUTTER_ROTATE_CW,
    start_angle, start_angle + 360);
  clutter_behaviour_ellipse_set_angle_tilt (CLUTTER_BEHAVIOUR_ELLIPSE (item->ellipse_behaviour), 
    CLUTTER_X_AXIS, -90);
  /* Note that ClutterAlpha has a floating reference, so we don't need to unref it. */

  clutter_behaviour_apply (item->ellipse_behaviour, item->actor);
}

void add_image_actors()
{
  int x = 20;
  int y = 0;
  gdouble angle = 0;
  GSList *list = list_items;
  while (list)
  {
    /* Add the actor to the stage: */
    Item *item = (Item*)list->data;
    ClutterActor *actor = item->actor;
    clutter_container_add_actor (CLUTTER_CONTAINER (stage), actor);

    /* Set an initial position: */
    clutter_actor_set_position (actor, x, y);
    y += 100;

    /* Allow the actor to emit events.
     * By default only the stage does this.
     */
    clutter_actor_set_reactive (actor, TRUE);

    /* Connect signal handlers for events: */
    g_signal_connect (actor, "button-press-event",
      G_CALLBACK (on_texture_button_press), item);

    add_to_ellipse_behaviour (timeline_rotation, angle, item);
    angle += angle_step;

    clutter_actor_show (actor);

    list = g_slist_next (list);
  }
}

gdouble angle_in_360(gdouble angle)
{
  gdouble result = angle;
  while(result >= 360)
    result -= 360;
  
  return result;
}

/* This signal handler is called when the item has finished 
 * moving up and increasing in size.
 */
void on_timeline_moveup_completed(ClutterTimeline* timeline G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED)
{
  /* Unref this timeline because we have now finished with it: */
  g_object_unref (timeline_moveup);
  timeline_moveup = NULL;

  g_object_unref (behaviour_scale);
  behaviour_scale = NULL;

  g_object_unref (behaviour_path);
  behaviour_path = NULL;

  g_object_unref (behaviour_opacity);
  behaviour_opacity = NULL;
}

/* This signal handler is called when the items have completely 
 * rotated around the ellipse.
 */
void on_timeline_rotation_completed(ClutterTimeline* timeline G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED)
{
  /* All the items have now been rotated so that the clicked item is at the
   * front.  Now we transform just this one item gradually some more, and
   * show the filename.
   */
  /* Transform the image: */
  ClutterActor *actor = item_at_front->actor;
  timeline_moveup = clutter_timeline_new(1000 /* milliseconds */);
  ClutterAlpha *alpha =
    clutter_alpha_new_full (timeline_moveup, CLUTTER_EASE_OUT_SINE);
 
  /* Scale the item from its normal scale to approximately twice the normal scale: */
  gdouble scale_start = 0;
  clutter_actor_get_scale (actor, &scale_start, NULL);
  const gdouble scale_end = scale_start * 1.8;

  behaviour_scale = clutter_behaviour_scale_new (alpha,
                                                 scale_start, scale_start,
                                                 scale_end, scale_end);
  clutter_behaviour_apply (behaviour_scale, actor);

  /* Move the item up the y axis: */
  ClutterKnot knots[2];
  knots[0].x = clutter_actor_get_x (actor);
  knots[0].y = clutter_actor_get_y (actor);
  knots[1].x = knots[0].x;
  knots[1].y = knots[0].y - 250;
  behaviour_path =
    clutter_behaviour_path_new_with_knots (alpha, knots, G_N_ELEMENTS(knots));
  clutter_behaviour_apply (behaviour_path, actor);

  /* Show the filename gradually: */
  clutter_text_set_text (CLUTTER_TEXT (label_filename), item_at_front->filepath);
  behaviour_opacity = clutter_behaviour_opacity_new (alpha, 0, 255);
  clutter_behaviour_apply (behaviour_opacity, label_filename);

  /* Start the timeline and handle its "completed" signal so we can unref it. */
  g_signal_connect (timeline_moveup, "completed", G_CALLBACK (on_timeline_moveup_completed), NULL);
  clutter_timeline_start (timeline_moveup);

  /* Note that ClutterAlpha has a floating reference so we don't need to unref it. */
}

void rotate_all_until_item_is_at_front(Item *item)
{
  g_return_if_fail (item);

  clutter_timeline_stop(timeline_rotation);

  /* Stop the other timeline in case that is active at the same time: */
  if(timeline_moveup)
    clutter_timeline_stop (timeline_moveup);

  clutter_actor_set_opacity (label_filename, 0);

  /* Get the item's position in the list: */
  const gint pos = g_slist_index (list_items, item);
  g_assert (pos != -1);

  if(!item_at_front && list_items)
    item_at_front = (Item*)list_items->data;

  gint pos_front = 0;
  if(item_at_front)
     pos_front = g_slist_index (list_items, item_at_front);
  g_assert (pos_front != -1);

  /* const gint pos_offset_before_start = pos_front - pos; */ 
  
  /* Calculate the end angle of the first item: */
  const gdouble angle_front = 180;
  gdouble angle_start = angle_front - (angle_step * pos_front);
  angle_start = angle_in_360 (angle_start);
  gdouble angle_end = angle_front - (angle_step * pos);

  gdouble angle_diff = 0;

  /* Set the end angles: */
  GSList *list = list_items;
  while (list)
  {
    Item *this_item = (Item*)list->data;

    /* Reset its size: */
    scale_texture_default (this_item->actor);

    angle_start = angle_in_360 (angle_start);
    angle_end = angle_in_360 (angle_end);

    /* Move 360 instead of 0 
     * when moving for the first time,
     * and when clicking on something that is already at the front.
     */
    if(item_at_front == item)
      angle_end += 360;

    clutter_behaviour_ellipse_set_angle_start (
      CLUTTER_BEHAVIOUR_ELLIPSE (this_item->ellipse_behaviour), angle_start);
    clutter_behaviour_ellipse_set_angle_end (
      CLUTTER_BEHAVIOUR_ELLIPSE (this_item->ellipse_behaviour), angle_end);

    if(this_item == item)
    {
      if(angle_start < angle_end)
        angle_diff =  angle_end - angle_start;
      else
        angle_diff = 360 - (angle_start - angle_end);

      /* printf("  debug: angle diff=%f\n", angle_diff); */

    }

    /* TODO: Set the number of frames, depending on the angle.
     * otherwise the actor will take the same amount of time to reach 
     * the end angle regardless of how far it must move, causing it to 
     * move very slowly if it does not have far to move.
     */
    angle_end += angle_step;
    angle_start += angle_step;
    list = g_slist_next (list);
  }

  /* Set the number of frames to be proportional to the distance to travel,
     so the speed is always the same: */
  gint pos_to_move = 0;
  if(pos_front < pos)
  {
     const gint count = g_slist_length (list_items);
     pos_to_move = count + (pos - pos_front);
  }
  else
  {
     pos_to_move = pos_front - pos;
  }

  clutter_timeline_set_duration (timeline_rotation, angle_diff * 0.2);

  /* Remember what item will be at the front when this timeline finishes: */
  item_at_front = item;

  clutter_timeline_start (timeline_rotation);
}

static gboolean
on_texture_button_press (ClutterActor *actor G_GNUC_UNUSED, ClutterEvent *event G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED)
{
  /* Ignore the events if the timeline_rotation is running (meaning, if the objects are moving),
   * to simplify things:
   */
  if(timeline_rotation && clutter_timeline_is_playing (timeline_rotation))
  {
    printf("on_texture_button_press(): ignoring\n");
    return FALSE;
  }
  else
    printf("on_texture_button_press(): handling\n");

  Item *item = (Item*)user_data;
  rotate_all_until_item_is_at_front (item);

  return TRUE;
}

int main(int argc, char *argv[])
{
  ClutterColor stage_color = { 0xB0, 0xB0, 0xB0, 0xff }; /* light gray */

  clutter_init (&argc, &argv);

  /* Get the stage and set its size and color: */
  stage = clutter_stage_get_default ();
  clutter_actor_set_size (stage, 800, 600);
  clutter_stage_set_color (CLUTTER_STAGE (stage), &stage_color);

  /* Create and add a label actor, hidden at first: */
  label_filename = clutter_text_new ();
  ClutterColor label_color = { 0x60, 0x60, 0x90, 0xff }; /* blueish */
  clutter_text_set_color (CLUTTER_TEXT (label_filename), &label_color);
  clutter_text_set_font_name (CLUTTER_TEXT (label_filename), "Sans 24");
  clutter_actor_set_position (label_filename, 10, 10);
  clutter_actor_set_opacity (label_filename, 0);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), label_filename);
  clutter_actor_show (label_filename);

  /* Add a plane under the ellipse of images: */
  ClutterColor rect_color = { 0xff, 0xff, 0xff, 0xff }; /* white */
  ClutterActor *rect = clutter_rectangle_new_with_color (&rect_color);
  clutter_actor_set_height (rect, ELLIPSE_HEIGHT + 20);
  clutter_actor_set_width (rect, clutter_actor_get_width (stage) + 100);
  /* Position it so that its center is under the images: */
  clutter_actor_set_position (rect, 
    -(clutter_actor_get_width (rect) - clutter_actor_get_width (stage)) / 2, 
    ELLIPSE_Y + IMAGE_HEIGHT - (clutter_actor_get_height (rect) / 2));
  /* Rotate it around its center: */
  clutter_actor_set_rotation (rect, CLUTTER_X_AXIS, -90, 0, (clutter_actor_get_height (rect) / 2), 0);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), rect);
  clutter_actor_show (rect);

  /* Show the stage: */
  clutter_actor_show (stage);

  timeline_rotation = clutter_timeline_new(2000 /* milliseconds */);
  g_signal_connect (timeline_rotation, "completed", G_CALLBACK (on_timeline_rotation_completed), NULL);

  /* Add an actor for each image: */
  load_images ("./images/");
  add_image_actors ();

  /* clutter_timeline_set_loop(timeline_rotation, TRUE); */

  /* Move them a bit to start with: */
  if(list_items)
    rotate_all_until_item_is_at_front ((Item*)list_items->data);


  /* Start the main loop, so we can respond to events: */
  clutter_main ();


  /* Free the list items and the list: */
  g_slist_foreach(list_items, on_foreach_clear_list_items, NULL);
  g_slist_free (list_items);

  g_object_unref (timeline_rotation);

  return EXIT_SUCCESS;

}
