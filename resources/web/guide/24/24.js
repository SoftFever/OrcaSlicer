function OnInit()
{
	//let strInput=JSON.stringify(cData);
	//HandleStudio(strInput);
	
	TranslatePage();
	
	RequestProfile();
}



function RequestProfile()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="request_userguide_profile";
	
	SendWXMessage( JSON.stringify(tSend) );
}

function HandleStudio( pVal )
{
//	alert(strInput);
//	alert(JSON.stringify(strInput));
//	
//	let pVal=IsJson(strInput);
//	if(pVal==null)
//	{
//		alert("Msg Format Error is not Json");
//		return;
//	}
	
	let strCmd=pVal['command'];
	//alert(strCmd);
	
	if(strCmd=='response_userguide_profile')
	{
		HandleModelList(pVal['response']);
	}
}

function ShowPrinterThumb(pItem, strImg)
{
	$(pItem).attr('src',strImg);
	$(pItem).attr('onerror',null);
}

function HandleModelList( pVal )
{
	if( !pVal.hasOwnProperty("model") )
		return;

    pModel=pVal['model'];
	
	let nTotal=pModel.length;
	let ModelHtml={};
	for(let n=0;n<nTotal;n++)
	{
		let OneModel=pModel[n];
		
		let strVendor=OneModel['vendor'];
		
		//Add Vendor Html Node
		if($(".OneVendorBlock[vendor='"+strVendor+"']").length==0)
		{
			let sVV=strVendor;
			if( sVV=="BBL" )
				sVV="Bambu Lab";			
			if( sVV=="Custom")
				sVV="Custom Printer";
			if( sVV=="Other")
				sVV="Orca colosseum";

			let HtmlNewVendor='<div class="OneVendorBlock" Vendor="'+strVendor+'">'+
'<div class="BlockBanner">'+
'	<div class="BannerBtns">'+
'		<div class="SmallBtn_Green trans" tid="t11" onClick="SelectPrinterAll('+"\'"+strVendor+"\'"+')">all</div>'+
'		<div class="SmallBtn trans" tid="t12" onClick="SelectPrinterNone('+"\'"+strVendor+"\'"+')">none</div>'+
'	</div>'+
'	<a>'+sVV+'</a>'+
'</div>'+
'<div class="PrinterArea">	'+
'</div>'+
'</div>';
			
			$('#Content').append(HtmlNewVendor);
		}
		
		let ModelName=OneModel['model'];
		
		//Collect Html Node Nozzel Html
		if( !ModelHtml.hasOwnProperty(strVendor))
			ModelHtml[strVendor]='';
			
		let NozzleArray=OneModel['nozzle_diameter'].split(';');
		let HtmlNozzel='';
		for(let m=0;m<NozzleArray.length;m++)
		{
			let nNozzel=NozzleArray[m];
			HtmlNozzel += '<div class="pNozzel TextS2"><input type="checkbox" model="' + OneModel['model'] + '" nozzel="' + nNozzel + '" vendor="' + strVendor +'" onclick="CheckBoxOnclick(this)" /><span>'+nNozzel+'</span><span class="trans" tid="t13">mm nozzle</span></div>';
		}
		
		let CoverImage=OneModel['cover'];
		ModelHtml[strVendor]+='<div class="PrinterBlock">'+
'	<div class="PImg"><img src="'+CoverImage+'"  /></div>'+
'    <div class="PName">'+OneModel['model']+'</div>'+ HtmlNozzel +'</div>';
	}
	
	//Update Nozzel Html Append
	for( let key in ModelHtml )
	{
		$(".OneVendorBlock[vendor='"+key+"'] .PrinterArea").append( ModelHtml[key] );
	}
	
	
	//Update Checkbox
	$('input').prop("checked", false);
	for(let m=0;m<nTotal;m++)
	{
		let OneModel=pModel[m];

		let SelectList=OneModel['nozzle_selected'];
		if(SelectList!='')
		{
			SelectList=OneModel['nozzle_selected'].split(';');
    		let nLen=SelectList.length;

		    for(let a=0;a<nLen;a++)
			{
			    let nNozzel=SelectList[a];
				$("input[vendor='" + OneModel['vendor'] + "'][model='" + OneModel['model'] + "'][nozzel='" + nNozzel + "']").prop("checked", true);

				SetModelSelect(OneModel['vendor'], OneModel['model'], nNozzel, true);
			}
		}
		else
		{
			$("input[vendor='"+OneModel['vendor']+"'][model='"+OneModel['model']+"']").prop("checked", false);
		}
	}	

	// let AlreadySelect=$("input:checked");
	// let nSelect=AlreadySelect.length;
	// if(nSelect==0)
	// {
	// 	$("input[nozzel='0.4'][vendor='Custom']").prop("checked", true);
	// }
	
	TranslatePage();
}

function CheckBoxOnclick(obj) {

	let strModel = obj.getAttribute("model");

	let strVendor = obj.getAttribute("vendor");
	let strNozzel = obj.getAttribute("nozzel");

	SetModelSelect(strVendor, strModel, strNozzel, obj.checked);

}

function SetModelSelect(vendor, model, nozzel, checked) {
	if (!ModelNozzleSelected.hasOwnProperty(vendor) && !checked) {
		return;
	}

	if (!ModelNozzleSelected.hasOwnProperty(vendor) && checked) {
		ModelNozzleSelected[vendor] = {};
	}

	let oVendor = ModelNozzleSelected[vendor];
	if (!oVendor.hasOwnProperty(model)) {
		oVendor[model] = {};
	}

	let oModel = oVendor[model];
	if (oModel.hasOwnProperty(nozzel) || checked) {
		oVendor[model][nozzel] = checked;
	}
}

function GetModelSelect(vendor, model, nozzel) {
	if (!ModelNozzleSelected.hasOwnProperty(vendor)) {
		return false;
	}

	let oVendor = ModelNozzleSelected[vendor];
	if (!oVendor.hasOwnProperty(model)) {
		return false;
	}

	let oModel = oVendor[model];
	if (!oModel.hasOwnProperty(nozzel)) {
		return false;
	}

	return oVendor[model][nozzel];
}

function FilterModelList(keyword) {

	//Save checkbox state
	let ModelSelect = $('input[type=checkbox]');
	for (let n = 0; n < ModelSelect.length; n++) {
		let OneItem = ModelSelect[n];

		let strModel = OneItem.getAttribute("model");

		let strVendor = OneItem.getAttribute("vendor");
		let strNozzel = OneItem.getAttribute("nozzel");

		SetModelSelect(strVendor, strModel, strNozzel, OneItem.checked);
	}

	let nTotal = pModel.length;
	let ModelHtml = {};

	$('#Content').empty();
	for (let n = 0; n < nTotal; n++) {
		let OneModel = pModel[n];

		let strVendor = OneModel['vendor'];
		let ModelName = OneModel['model'];
		if (ModelName.toLowerCase().indexOf(keyword.toLowerCase()) == -1)
			continue;

		//Add Vendor Html Node
		if ($(".OneVendorBlock[vendor='" + strVendor + "']").length == 0) {
			let sVV = strVendor;
			if (sVV == "BBL")
				sVV = "Bambu Lab";
			if (sVV == "Custom")
				sVV = "Custom Printer";
			if (sVV == "Other")
				sVV = "Orca colosseum";

			let HtmlNewVendor = '<div class="OneVendorBlock" Vendor="' + strVendor + '">' +
				'<div class="BlockBanner">' +
				'	<div class="BannerBtns">' +
				'		<div class="SmallBtn_Green trans" tid="t11" onClick="SelectPrinterAll(' + "\'" + strVendor + "\'" + ')">all</div>' +
				'		<div class="SmallBtn trans" tid="t12" onClick="SelectPrinterNone(' + "\'" + strVendor + "\'" + ')">none</div>' +
				'	</div>' +
				'	<a>' + sVV + '</a>' +
				'</div>' +
				'<div class="PrinterArea">	' +
				'</div>' +
				'</div>';

			$('#Content').append(HtmlNewVendor);
		}

		//Collect Html Node Nozzel Html
		if (!ModelHtml.hasOwnProperty(strVendor))
			ModelHtml[strVendor] = '';

		let NozzleArray = OneModel['nozzle_diameter'].split(';');
		let HtmlNozzel = '';
		for (let m = 0; m < NozzleArray.length; m++) {
			let nNozzel = NozzleArray[m];
			HtmlNozzel += '<div class="pNozzel TextS2"><input type="checkbox" model="' + OneModel['model'] + '" nozzel="' + nNozzel + '" vendor="' + strVendor + '" onclick="CheckBoxOnclick(this)" /><span>' + nNozzel + '</span><span class="trans" tid="t13">mm nozzle</span></div>';
		}

		let CoverImage = OneModel['cover'];
		ModelHtml[strVendor] += '<div class="PrinterBlock">' +
			'	<div class="PImg"><img src="' + CoverImage + '"  /></div>' +
			'    <div class="PName">' + OneModel['model'] + '</div>' + HtmlNozzel + '</div>';
	}

	//Update Nozzel Html Append
	for (let key in ModelHtml) {
		let obj = $(".OneVendorBlock[vendor='" + key + "'] .PrinterArea");
		obj.empty();
		obj.append(ModelHtml[key]);
	}


	//Update Checkbox
	ModelSelect = $('input[type=checkbox]');
	for (let n = 0; n < ModelSelect.length; n++) {
		let OneItem = ModelSelect[n];

		let strModel = OneItem.getAttribute("model");
		let strVendor = OneItem.getAttribute("vendor");
		let strNozzel = OneItem.getAttribute("nozzel");

		let checked = GetModelSelect(strVendor, strModel, strNozzel);

		OneItem.checked = checked;
	}

	// let AlreadySelect=$("input:checked");
	// let nSelect=AlreadySelect.length;
	// if(nSelect==0)
	// {
	// 	$("input[nozzel='0.4'][vendor='Custom']").prop("checked", true);
	// }

	TranslatePage();
}

function SelectPrinterAll( sVendor )
{
	$("input[vendor='"+sVendor+"']").prop("checked", true);
}


function SelectPrinterNone( sVendor )
{
	$("input[vendor='"+sVendor+"']").prop("checked", false);
}

function OnExitFilter() {

	let nTotal = 0;
	let ModelAll = {};
	for (vendor in ModelNozzleSelected) {
		for (model in ModelNozzleSelected[vendor]) {
			for (nozzel in ModelNozzleSelected[vendor][model]) {
				if (!ModelNozzleSelected[vendor][model][nozzel])
					continue;

				if (!ModelAll.hasOwnProperty(model)) {
					//alert("ADD: "+strModel);

					ModelAll[model] = {};

					ModelAll[model]["model"] = model;
					ModelAll[model]["nozzle_diameter"] = '';
					ModelAll[model]["vendor"] = vendor;
				}

				ModelAll[model]["nozzle_diameter"] += ModelAll[model]["nozzle_diameter"] == '' ? nozzel : ';' + nozzel;

				nTotal++;
			}

		}
	}

	var tSend = {};
	tSend['sequence_id'] = Math.round(new Date() / 1000);
	tSend['command'] = "save_userguide_models";
	tSend['data'] = ModelAll;

	SendWXMessage(JSON.stringify(tSend));

	return nTotal;

}

//
function OnExit()
{	
	let ModelAll={};
	
	let ModelSelect=$("input:checked");
	let nTotal=ModelSelect.length;

	if( nTotal==0 )
	{
		ShowNotice(1);
		
		return 0;
	}
	
	for(let n=0;n<nTotal;n++)
	{
	    let OneItem=ModelSelect[n];
		
		let strModel=OneItem.getAttribute("model");
		let strVendor=OneItem.getAttribute("vendor");
		let strNozzel=OneItem.getAttribute("nozzel");
			
		//alert(strModel+strVendor+strNozzel);
		
		if(!ModelAll.hasOwnProperty(strModel))
		{
			//alert("ADD: "+strModel);
			
			ModelAll[strModel]={};
		
			ModelAll[strModel]["model"]=strModel;
			ModelAll[strModel]["nozzle_diameter"]='';
			ModelAll[strModel]["vendor"]=strVendor;
		}
		
		ModelAll[strModel]["nozzle_diameter"]+=ModelAll[strModel]["nozzle_diameter"]==''?strNozzel:';'+strNozzel;
	}
		
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="save_userguide_models";
	tSend['data']=ModelAll;
	
	SendWXMessage( JSON.stringify(tSend) );

    return nTotal;
}


function ShowNotice( nShow )
{
	if(nShow==0)
	{
		$("#NoticeMask").hide();
		$("#NoticeBody").hide();
	}
	else
	{
		$("#NoticeMask").show();
		$("#NoticeBody").show();
	}
}

function CancelSelect()
{
	var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="user_guide_cancel";
	tSend['data']={};
		
	SendWXMessage( JSON.stringify(tSend) );			
}


function ConfirmSelect()
{
	let nChoose=OnExitFilter();
	
	if(nChoose>0)
    {
		var tSend={};
		tSend['sequence_id']=Math.round(new Date() / 1000);
		tSend['command']="user_guide_finish";
		tSend['data']={};
		tSend['data']['action']="finish";
		
		SendWXMessage( JSON.stringify(tSend) );			
	}
}




