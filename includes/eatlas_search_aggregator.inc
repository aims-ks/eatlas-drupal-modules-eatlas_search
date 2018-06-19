<?php

// NOTE: There is very little support for aggregator items in the API. Pretty much everything needs to be hardcoded.
function _eatlas_search_aggregator_item_to_result($item, $keys) {
	$item = _eatlas_search_load_aggregator_item($item);
	$title = $item['title'];
	$link = $item['link'];
	$desc = $item['description'];

	return array(
		'link' => $link,
		'type' => 'Metadata',
		'title' => $title . ' [' . $item['iid'] . ']',
		'item' => $item,
		'score' => $item['calculated_score'],
		'snippet' => _eatlas_search_excerpt($keys, $desc),
	);
}

// NOTE: There is no method to load an aggregator item in Drupal API.
//     Drupal itself use a SQL query like this one.
// See:
//     File: /www/modules/aggregator/aggregator.module
//     Method: aggregator_block_view
//     Line: ~405
function _eatlas_search_load_aggregator_item($item) {
	$query = db_select('aggregator_item', 'a')
		->fields('a'); // equivalent to "a.*"
	$query->condition('a.iid', $item->sid, '=');

	$result = $query->execute()->fetchAssoc();
	$result['calculated_score'] = $item->calculated_score;
	return $result;
}

/**
 * Implements: hook_aggregator_remove($feed).
 * Trigger when an aggregator feed is deleted or its items are deleted.
 * Since there is not API triggered on update, there is
 * no way to flag only the feed items that has changed.
 * We need to flag them all for re-indexation. This is
 * sub-optimal, aggregator items should never be used in search.
 */
function eatlas_search_aggregator_remove($feed) {
	if (_eatlas_search_aggregator_feed_is_indexed($feed->fid)) {
		watchdog('eatlas_search', 'Indexed feed modified. Every single indexed feed need to be re-indexed.', array(), WATCHDOG_WARNING);

		db_update('search_dataset')
			->fields(array('reindex' => REQUEST_TIME))
			->condition('type', EATLAS_SEARCH_INDEX_RSS, '=')
			->execute();
	}
}
function _eatlas_search_aggregator_feed_is_indexed($fid) {
	$searchConfig = _eatlas_search_get_advanced_search_config();
	foreach ($searchConfig as $searchItem) {
		if (isset($searchItem['feed_ids'])) {
			foreach ($searchItem['feed_ids'] as $feed_id) {
				if (intval($feed_id) === intval($fid)) {
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}



/**************
 * INDEXATION *
 **************/



// Status of "ea_rss" index
function _eatlas_search_aggregator_status() {
	$searchConfig = _eatlas_search_get_advanced_search_config();

	$feedIds = array();
	foreach ($searchConfig as $searchItem) {
		if (isset($searchItem['feed_ids'])) {
			foreach ($searchItem['feed_ids'] as $feed_id) {
				$feedIds[] = $feed_id;
			}
		}
	}

	$total = 0;
	$remaining = 0;
	if (!empty($feedIds)) {
		$query = db_select('aggregator_item', 'a')
			->condition('a.fid', $feedIds, 'IN');
		$query->addExpression('COUNT(*)', 'count');
		$result = $query->execute();
		$total = $result->fetchField();

		$query = db_select('aggregator_item', 'a')
			->condition('a.fid', $feedIds, 'IN');
		$query->leftJoin('search_dataset', 'd', "d.type = '".EATLAS_SEARCH_INDEX_RSS."' AND d.sid = a.iid");
		$query->addExpression('COUNT(*)', 'count');
		$or = db_or();
		$or->isNull('d.sid');
		$or->condition('d.reindex', '0', '<>');
		$query->condition($or);
		$result = $query->execute();
		$remaining = $result->fetchField();
	}

	return array('remaining' => $remaining, 'total' => $total);
}



// Update medatata (rss items) indexes
function _eatlas_search_aggregator_update_index($limit) {
	$searchConfig = _eatlas_search_get_advanced_search_config();

	$feedIds = array();
	foreach ($searchConfig as $searchItem) {
		if (isset($searchItem['feed_ids'])) {
			foreach ($searchItem['feed_ids'] as $feed_id) {
				$feedIds[] = $feed_id;
			}
		}
	}

	if (!empty($feedIds)) {
		$query = db_select('aggregator_item', 'a')
			->fields('a', array('iid', 'title', 'link', 'description'))
			->condition('a.fid', $feedIds, 'IN')
			->orderBy('d.reindex')
			->orderBy('a.iid');
		$query->leftJoin('search_dataset','d',"d.type = '".EATLAS_SEARCH_INDEX_RSS."' AND d.sid = a.iid");
		$or = db_or();
		$or->isNull('d.sid');
		$or->condition('d.reindex', '0', '<>');
		$query->condition($or);
		$query->range(0, $limit);

		$result = $query->execute();

		foreach ($result as $item) {
			_eatlas_search_index_aggregator_item($item, EATLAS_SEARCH_INDEX_RSS);
		}
	}
}
function _eatlas_search_index_aggregator_item($item, $index) {
	$title = $item->title;
	$description = $item->description;

	$text = '<h1>' . $title . '</h1>' .
			$description;

	// Update index
	_eatlas_search_index($item->iid, $index, $text);
}

?>
