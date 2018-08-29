<?php

// NOTE: There is no "Manage Display" for search results in file types. The fields to display needs to be hardcoded.
function _eatlas_search_file_to_result($item, $keys) {
	$file = file_load($item->sid);

	$filename = isset($file->filename) ? $file->filename : '';

	// Get the file title and description.
	$title = '';
	$titleValues = field_get_items('file', $file, 'media_title');
	if (isset($titleValues[0]['value'])) {
		// We need the raw value because the output will be escaped. (issue with chars like "&")
		$title = $titleValues[0]['value'];
	}

	if (!$title) {
		$title = $filename;
	}

	$description = '';
	$descriptionValues = field_get_items('file', $file, 'media_description');
	if ($descriptionValues) {
		$description_field_view = field_view_value('file', $file, 'media_description', $descriptionValues[0]);
		$description = render($description_field_view);
	}

	// NOTE: The short description should not have anything not already in the description. We only use it in the rare case of missing description.
	if (!$description) {
		$shortDescription = '';
		$shortDescriptionValues = field_get_items('file', $file, 'field_short_description');
		if ($shortDescriptionValues) {
			$shortDescription_field_view = field_view_value('file', $file, 'field_short_description', $shortDescriptionValues[0]);
			$shortDescription = render($shortDescription_field_view);
		}
		$description = $shortDescription;
	}

	$licence = NULL;
	if (module_exists('media_gallery_fixes')) {
		$licence = theme('media_gallery_fixes_license', array('file' => $file, 'color' => 'dark'));
	}

	$uri = entity_uri('file', $file);

	$file_types_info = file_info_file_types();
	$file_type = file_get_type($file);
	$file_type_label = $file_type;
	if (isset($file_types_info[$file_type]['label'])) {
		$file_type_label = 'File: ' . $file_types_info[$file_type]['label'];
	}

	return array(
		'link' => url($uri['path'], array_merge($uri['options'], array('absolute' => TRUE))),
		'type' => check_plain($file_type_label),
		'title' => $title,
		'user' => $licence,
		'file' => $file,
		'score' => $item->calculated_score,
		// strip_tags: Remove HTML tags
		//     http://ua2.php.net/manual/en/function.strip-tags.php
		'snippet' => _eatlas_search_excerpt($keys, strip_tags($description))
	);
}

/**
 * Implements hook_file_update().
 * Triggered when a file is modified.
 * Used to flag index item for reindexation when a file is modified.
 * The re-indexation is done with the cron (run automatically or manually)
 */
function eatlas_search_file_update($file) {
	db_update('search_dataset')
		->fields(array('reindex' => REQUEST_TIME))
		->condition('sid', $file->fid)
		->condition('type', EATLAS_SEARCH_INDEX_FILE, '=')
		->execute();
}
// hook_file_delete($file)
function eatlas_search_file_delete($file) {
	_eatlas_search_delete_index($file->fid, EATLAS_SEARCH_INDEX_FILE);
}


/**************
 * INDEXATION *
 **************/

function _eatlas_search_files_status() {
	$total = db_query('SELECT COUNT(*) FROM {file_managed}')->fetchField();
	$remaining = db_query("SELECT COUNT(*) FROM {file_managed} f LEFT JOIN {search_dataset} d ON d.type = '".EATLAS_SEARCH_INDEX_FILE."' AND d.sid = f.fid WHERE d.sid IS NULL OR d.reindex <> 0")->fetchField();
	return array('remaining' => $remaining, 'total' => $total);
}



// Update file indexes - index ALL files whatever it's type, the search filter the type
function _eatlas_search_file_update_index($limit) {
	$result = db_query_range("SELECT f.fid FROM {file_managed} f LEFT JOIN {search_dataset} d ON d.type = '".EATLAS_SEARCH_INDEX_FILE."' AND d.sid = f.fid WHERE d.sid IS NULL OR d.reindex <> 0 ORDER BY d.reindex ASC, f.fid ASC", 0, $limit, array(), array('target' => 'slave'));

	foreach ($result as $file) {
		_eatlas_search_index_file($file);
	}
}
function _eatlas_search_index_file($file) {
	$file = file_load($file->fid);

	$filename = $file->filename;

	// Get the file title and description.
	$title = '';
	$titleValues = field_get_items('file', $file, 'media_title');
	if ($titleValues) {
		$title_field_view = field_view_value('file', $file, 'media_title', $titleValues[0]);
		$title = render($title_field_view);
	}

	$description = '';
	$descriptionValues = field_get_items('file', $file, 'media_description');
	if ($descriptionValues) {
		$description_field_view = field_view_value('file', $file, 'media_description', $descriptionValues[0]);
		$description = render($description_field_view);
	}

	$shortDescription = '';
	$shortDescriptionValues = field_get_items('file', $file, 'field_short_description');
	if ($shortDescriptionValues) {
		$shortDescription_field_view = field_view_value('file', $file, 'field_short_description', $shortDescriptionValues[0]);
		$shortDescription = render($shortDescription_field_view);
	}

	$tags = '';
	$tagsValues = field_get_items('file', $file, 'field_tags');
	if ($tagsValues) {
		foreach ($tagsValues as $delta => $tagValue) {
			// NOTE: "render" produce an HTML link to the term. We only want the text value to index it.
			$tagViewValue = field_view_value('file', $file, 'field_tags', $tagValue);
			if (isset($tagViewValue['#title'])) {
				$tags .= ' ' . $tagViewValue['#title'];
			}
		}
	}

	$text = '<h1>' . $title . '</h1>' .
			$description . ' ' .
			$shortDescription . ' ' .
			$filename . ' ' .
			$tags; // This is not be best way to index tags but it's easier than modifying the DB structure

	// Update index
	_eatlas_search_index($file->fid, EATLAS_SEARCH_INDEX_FILE, $text);
}

?>
