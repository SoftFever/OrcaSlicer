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


function HandleModelList( pVal )
{
	if( !pVal.hasOwnProperty("model") )
		return;

    let pModel=pVal['model'];
	
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
			HtmlNozzel+='<div class="pNozzel TextS2"><input type="checkbox" model="'+OneModel['model']+'" nozzel="'+nNozzel+'" vendor="'+strVendor+'" /><span>'+nNozzel+'</span><span class="trans" tid="t13">mm nozzle</span></div>';
		}
		
		let CoverImage="../../image/printer/"+OneModel['model']+"_cover.png";
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
			    $("input[vendor='"+OneModel['vendor']+"'][model='"+OneModel['model']+"'][nozzel='"+nNozzel+"']").prop("checked", true);
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


function SelectPrinterAll( sVendor )
{
	$("input[vendor='"+sVendor+"']").prop("checked", true);
}


function SelectPrinterNone( sVendor )
{
	$("input[vendor='"+sVendor+"']").prop("checked", false);
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
	let nChoose=OnExit();
	
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




