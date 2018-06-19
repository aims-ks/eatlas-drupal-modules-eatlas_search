<?php

/**
 * This file used fields that are highly specific to the eAtlas.
 */

/**
 * Convert a basic search result to a displayable result.
 * This is very specific to "eAtlas". I will need to add
 * some config before distribute it.
 */
function _eatlas_search_node_to_result($item, $keys) {
	// Render the node.
	$node = node_load($item->sid);
	$build = node_view($node, 'eatlas_search_result');
	if (isset($build['field_authors'])) {
		hide($build['field_authors']);
	}
	if (isset($build['field_project_leaders'])) {
		hide($build['field_project_leaders']);
	}
	if (isset($build['field_published_date'])) {
		hide($build['field_published_date']);
	}
	// Person node type
	if (isset($build['field_organisation'])) {
		hide($build['field_organisation']);
	}
	// Person node type
	if (isset($build['field_web_site'])) {
		hide($build['field_web_site']);
	}

	unset($build['#theme']);
	$node->rendered = drupal_render($build);

	// Fetch comments for snippet.
	$node->rendered .= ' ' . module_invoke('comment', 'node_update_index', $node);

	$extra = module_invoke_all('node_search_result', $node);

	$authors_list = '';
	$authors = field_get_items('node', $node, 'field_authors');
	if ($authors) {
		foreach($authors as $author) {
			if ($authors_list) {
				$authors_list .= ', ';
			}
			$authors_list_field_view = field_view_value('node', $node, 'field_authors', $author, 'eatlas_search_result');
			$authors_list .= render($authors_list_field_view);
		}
	}
	$leaders = field_get_items('node', $node, 'field_project_leaders');
	if ($leaders) {
		foreach($leaders as $leader) {
			if ($authors_list) {
				$authors_list .= ', ';
			}
			$authors_list_field_view = field_view_value('node', $node, 'field_project_leaders', $leader, 'eatlas_search_result');
			$authors_list .= render($authors_list_field_view);
		}
	}

	// Add the published date to the extra field, using the display defined for "search_result" in "Structure > Content types > XXX > Manage Display > Search result"
	$published_field = field_get_items('node', $node, 'field_published_date');
	if ($published_field) {
		$published_value = field_view_value('node', $node, 'field_published_date', $published_field[0], 'eatlas_search_result');
		$extra[] = render($published_value);
	}

	$link = NULL;
	$rss_link_field = field_get_items('node', $node, 'field_ea_rss_link');
	if ($rss_link_field) {
		$rss_link_output = field_view_value('node', $node, 'field_ea_rss_link', $rss_link_field[0]);

		// Adjust the search result link
		$link = render($rss_link_output);
	} else {
		$uri = entity_uri('node', $node);
		$link = url($uri['path'], array_merge($uri['options'], array('absolute' => TRUE)));
	}
	return array(
		'link' => $link,
		'type' => _eatlas_search_node_get_node_display_type($node),
		'title' => _eatlas_search_node_get_node_display_title($node),
		//'user' => theme('username', array('account' => $node)),
		'user' => $authors_list,
		//'date' => $node->changed, // Required a timestamp - display mode not configurable (better use extra field)
		'node' => $node,
		'extra' => $extra,
		'score' => $item->calculated_score,
		'snippet' => _eatlas_search_excerpt($keys, $node->rendered),
		'language' => entity_language('node', $node)
	);
}

/**
 * NOTE: Passing the object by value DO NOT use more memory; it
 *     sent a copy of the pointer, not a copy of the whole object.
 */
function _eatlas_search_node_get_node_display_type($node) {
	if ($node->type === 'eatlas_georss_item') {
		// RSS Item (as node), we want the name of the feed instead of
		// the generic name of the node type.
		$field_fid = field_get_items('node', $node, 'field_ea_rss_fid');
		if ($field_fid && isset($field_fid[0]['value']) && is_numeric($field_fid[0]['value'])) {
			$feed_obj = aggregator_feed_load($field_fid[0]['value']);
			if (property_exists($feed_obj, 'title') && $feed_obj->title) {
				return check_plain($feed_obj->title);
			}
		}
	}
	return check_plain(node_type_get_name($node));
}

function _eatlas_search_node_get_node_display_title($node) {
	if ($node->type === 'organisation' || $node->type === 'organisation_section') {
		if (property_exists($node, 'field_name') && isset($node->field_name[LANGUAGE_NONE][0]['value'])) {
			return $node->field_name[LANGUAGE_NONE][0]['value'];
		}
	}
	return $node->title;
}

/**
 * Implements hook_node_update().
 * Triggered when a node is modified.
 * Used to flag index item for reindexation when a node is modified.
 * The re-indexation is done with the cron (run automatically or manually)
 */
function eatlas_search_node_update($node) {
	// Book page - Flag the book cover for re-indexation
	if ($node->type === 'book') {
		// Don't index orphan book pages
		if (!property_exists($node, 'book') || !isset($node->book['bid'])) {
			return;
		}
		$node = node_load($node->book['bid']);
	}

	db_update('search_dataset')
		->fields(array('reindex' => REQUEST_TIME))
		->condition('sid', $node->nid)
		->condition('type', EATLAS_SEARCH_INDEX_NODE, '=')
		->execute();
}

/**
 * hook_node_delete()
 * Mark the book has been updated (for reindexation) when a page is
 * deleted from the book.
 */
function eatlas_search_node_delete($node) {
	if ($node->type === 'book') {
		// Book page - Flag the book cover for re-indexation
		eatlas_search_node_update($node);
	} else {
		// Other node - Delete the index
		_eatlas_search_delete_index($node->nid, EATLAS_SEARCH_INDEX_NODE);
	}
}


/**************
 * Book pages *
 **************/

/**
 * Implements hook_menu_link_insert().
 * Mark the book has been updated (for reindexation) when a page is
 * attached to the book.
 *   https://api.drupal.org/api/drupal/modules%21system%21system.api.php/function/hook_menu_link_insert/7
 */
function eatlas_search_menu_link_insert($link) {
	// bid => Point to the book
	// original_bid => Value of 0 - should not be used
	// link_path => Point to the book page - may be used as a fallback
	_eatlas_search_menu_link_modified($link);
}

/**
 * Implements hook_menu_link_update().
 * Trigger when a book page is moved from a book to another book.
 * Unfortunately, the hook do not provide any information
 *   https://api.drupal.org/api/drupal/modules%21system%21system.api.php/function/hook_menu_link_update/7
 */
function eatlas_search_menu_link_update($link) {
	// bid => Point to the new book
	// original_bid => Point to the old book
	// link_path => Point to the book page, linked to the original book - it's better to use original_bid
	_eatlas_search_menu_link_modified($link);
}

/**
 * Implements hook_menu_link_delete().
 * Mark the book has been updated (for reindexation) when a page is
 * deleted or detached from the book.
 *   https://api.drupal.org/api/drupal/modules%21system%21system.api.php/function/hook_menu_link_delete/7
 */
function eatlas_search_menu_link_delete($link) {
	// bid => N/A
	// original_bid => N/A
	// link_path => Point to the book page, before it is detached to the book.
	_eatlas_search_menu_link_modified($link);
}

function _eatlas_search_menu_link_modified($link) {
	if (isset($link['module']) && $link['module'] === 'book') {
		if (isset($link['bid']) && $link['bid']) {
			$book_node = node_load($link['bid']);
			eatlas_search_node_update($book_node);
		} elseif (isset($link['link_path'])) {
			$book_page_node = menu_get_object('node', 1, $link['link_path']);
			eatlas_search_node_update($book_page_node);
		}

		if (isset($link['original_bid']) && $link['original_bid']) {
			$original_book_node = node_load($link['original_bid']);
			eatlas_search_node_update($original_book_node);
		}
	}
}


/**************
 * INDEXATION *
 **************/

/**
 * Return an associative array of nodes that should not be indexed
 * (blacklisted). The list includes the 404 page and the 403 page,
 * if defined in the site configuration.
 * Structure of the array returned:
 *     Key: Node ID
 *     Value: Node object
 * WARNING: The array might be empty!
 */
function _eatlas_search_node_get_blacklisted_nodes() {
	$blacklist = array();

	if ($path404 = variable_get('site_404', NULL)) {
		$node404 = menu_get_object('node', 1, $path404);
		if ($node404 && property_exists($node404, 'nid')) {
			$blacklist[$node404->nid] = $node404;
		}
	}
	if ($path403 = variable_get('site_403', NULL)) {
		$node403 = menu_get_object('node', 1, $path403);
		if ($node403 && property_exists($node403, 'nid')) {
			$blacklist[$node403->nid] = $node403;
		}
	}

	return $blacklist;
}

/**
 * Return the index status for nodes.
 * Example:
 *     array(
 *         'remaining' => 10,
 *         'total' => 100
 *     );
 */
function _eatlas_search_nodes_status() {
	$blacklist = _eatlas_search_node_get_blacklisted_nodes();
	$blacklist_keys = array_keys($blacklist);

	$total_sql = 'SELECT COUNT(*) FROM {node}';
	$total_args = array();

	// Ignore book pages, they are indexed with the book cover.
	$remaining_sql = 'SELECT COUNT(*) FROM {node} n '.
		'LEFT JOIN {search_dataset} d ON d.type = :type AND d.sid = n.nid '.
		'WHERE (d.sid IS NULL OR d.reindex <> 0) AND n.type <> :book';
	$remaining_args = array(
		':type' => EATLAS_SEARCH_INDEX_NODE,
		':book' => 'book'
	);

	// Add a filter for blacklisted nodes only if there is some blacklisted nodes.
	if (!empty($blacklist_keys)) {
		$total_sql .= ' WHERE nid NOT IN (:nids)';
		$total_args[':nids'] = $blacklist_keys;

		$remaining_sql .= ' AND n.nid NOT IN (:nids)';
		$remaining_args[':nids'] = $blacklist_keys;
	}

	$total = db_query($total_sql, $total_args)->fetchField();
	$remaining = db_query($remaining_sql, $remaining_args)->fetchField();

	return array('remaining' => $remaining, 'total' => $total);
}

/**
 * Update all node indexes
 * index ALL nodes whatever it's type, the search will filter the type
 */
function _eatlas_search_node_update_index($limit) {
	$blacklist = _eatlas_search_node_get_blacklisted_nodes();
	$blacklist_keys = array_keys($blacklist);

	$query_sql_select = 'SELECT n.nid FROM {node} n LEFT JOIN {search_dataset} d ON d.type = :type AND d.sid = n.nid';
	// Do not index book pages, they are indexed with the book cover.
	$query_sql_where = 'WHERE (d.sid IS NULL OR d.reindex <> 0) AND n.type <> :book';
	$query_sql_order = 'ORDER BY d.reindex ASC, n.nid ASC';
	$query_args = array(
		':type' => EATLAS_SEARCH_INDEX_NODE,
		':book' => 'book'
	);

	// Add a filter for blacklisted nodes only if there is some blacklisted nodes.
	if (!empty($blacklist_keys)) {
		// Remove blacklisted nodes from the indexes
		foreach ($blacklist_keys as $blacklist_nid) {
			_eatlas_search_delete_index($blacklist_nid, EATLAS_SEARCH_INDEX_NODE);
		}

		$query_sql_where .= ' AND n.nid NOT IN (:nids)';
		$query_args[':nids'] = $blacklist_keys;
	}

	$result = db_query_range("$query_sql_select $query_sql_where $query_sql_order",
			0, $limit, $query_args, array('target' => 'slave'));

	if ($result) {
		foreach ($result as $node) {
			_eatlas_search_index_node($node);
		}
	}
}

/**
 * Update a single node index.
 * NOTE: Book are index with all their pages.
 */
function _eatlas_search_index_node($node) {
	$node = node_load($node->nid);

	// Save the changed time of the most recent indexed node, for the search
	// results half-life calculation.
	variable_set('node_cron_last', $node->changed);

	// We don't know if the node is a book or not, so we just try to
	// load the book pages. If that doesn't give anything,
	// it's not a book. (the book API seems quite limited)
	// NOTE: 10 is the max depth of the tree. The maximum allowed
	//     depth is 9, so 10 should always be enough.
	$book_toc = book_toc($node->nid, 10);
	$text = '';
	if ($book_toc) {
		foreach($book_toc as $mlid => $title) {
			// Load a "menu link" from a menu link ID.
			$menu_link = menu_link_load($mlid);
			// Load a node from a node path
			$page = menu_get_object('node', 1, $menu_link['link_path']);

			$text .= _eatlas_search_node_get_index_text($page);
		}
	} else {
		// Not a book
		$text = _eatlas_search_node_get_index_text($node);
	}

	// Update index
	_eatlas_search_index($node->nid, EATLAS_SEARCH_INDEX_NODE, $text);
}

// Return a chuck of text representing the node, ready for indexation.
function _eatlas_search_node_get_index_text($node) {
	// Render the node.
	$build = node_view($node, 'eatlas_search_index');
	unset($build['#theme']);
	$node->rendered = drupal_render($build);

	// H1 as higher priority in the index (when doing a search)
	$text = '<h1>' . check_plain($node->title) . '</h1>' . $node->rendered;

	// Fetch extra data normally not visible
	$extra = module_invoke_all('node_update_index', $node);
	foreach ($extra as $t) {
		$text .= $t;
	}

	return $text;
}

?>
