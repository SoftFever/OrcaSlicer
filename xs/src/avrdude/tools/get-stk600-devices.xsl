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
    * Extract STK600 device support out of
    * targetboards.xml.
    *
    * Run this like:
    *
    * xsltproc \
    *   tools/get-stk600-devices.xsl targetboard.xml
    *
    * and copy&paste the results into the respective table.
-->
<xsl:output method="text"/>
<xsl:template match="/">
    <xsl:text>@multitable @columnfractions .15 .15 .6&#010;</xsl:text>
    <xsl:text>Routing card @tab Socket card @tab Devices&#010;</xsl:text>
    <xsl:for-each select="/STK600/ROUTING/CARD">
	<xsl:text>@item @code{</xsl:text>
	<xsl:value-of select="RC_NAME" />
	<xsl:text>} @tab @code{</xsl:text>
	<xsl:value-of select="SC_NAME" />
	<xsl:text>} @tab</xsl:text>
        <xsl:for-each select="TARGET">
           <xsl:text> </xsl:text>
           <xsl:value-of select="NAME" />
        </xsl:for-each>
        <xsl:text>&#010;</xsl:text>
    </xsl:for-each>  <!-- All cards -->
    <xsl:text>@end multitable&#010;</xsl:text>

</xsl:template>
</xsl:stylesheet>
