<?xml version="1.0" encoding='ISO-8859-1' ?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<!--
 * Copyright (c) 2008 Joerg Wunsch
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * $Id$
-->
<!--
    * Extract STK600 routing and socket card information out of
    * targetboards.xml.
    *
    * Run this like:
    *
    * xsltproc -param what "'RC'" \
    *   tools/get-stk600-cards.xsl targetboard.xml | sort -u
    *
    * xsltproc -param what "'SC'" \
    *   tools/get-stk600-cards.xsl targetboard.xml | sort -u
    *
    * and copy&paste the results into the respective tables.
-->
<xsl:output method="text"/>
<xsl:template match="/">
  <xsl:if test="$what = 'RC'">
    <xsl:for-each select="/STK600/ROUTING/CARD">
      <xsl:if test="RC_NAME != ''">
	<xsl:text>  { </xsl:text>
	<xsl:value-of select="RC_ID" />
	<xsl:text>, &#034;</xsl:text>
	<xsl:value-of select="RC_NAME" />
	<xsl:text>&#034; },&#010;</xsl:text>
      </xsl:if>
    </xsl:for-each>  <!-- All cards -->
  </xsl:if> <!-- Routing cards -->

  <xsl:if test="$what = 'SC'">
  <xsl:for-each select="/STK600/ROUTING/CARD">
    <xsl:if test="SC_NAME != ''">
      <xsl:text>  { </xsl:text>
      <xsl:value-of select="SC_ID" />
      <xsl:text>, &#034;</xsl:text>
      <xsl:value-of select="SC_NAME" />
      <xsl:text>&#034; },&#010;</xsl:text>
    </xsl:if>
  </xsl:for-each>  <!-- All cards -->
  </xsl:if> <!-- Socket cards -->

</xsl:template>
</xsl:stylesheet>
