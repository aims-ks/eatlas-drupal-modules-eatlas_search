<?php
/**
 * List all character substitution.
 * It's just to make search easier.
 */
function _eatlas_search_replace_unicode($text) {
	return str_replace(array(
			'’'
		), array(
			'\''
		), $text);
}

?>
