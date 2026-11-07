#include "PCH.h"

#include "Config.h"

#include <toml++/toml.hpp>

#include <fstream>

namespace
{
	Config::Settings g_s;

	std::filesystem::path Path()
	{
		return std::filesystem::path("Data/SKSE/Plugins/TongueLab.toml");
	}

	std::filesystem::path LoadPath()
	{
		return Path();
	}
}

namespace Config
{
	const Settings& Get() { return g_s; }
	Settings&       Edit() { return g_s; }

	void Load()
	{
		toml::table tbl;
		try {
			tbl = toml::parse_file(LoadPath().string());
		} catch (const toml::parse_error& e) {
			logger::warn("TongueLab.toml not read ({}): using defaults", e.description());
			return;
		}
		Settings s;
		s.enabled = tbl["General"]["Enabled"].value_or(s.enabled);
		s.debug = tbl["General"]["Debug"].value_or(s.debug);
		s.mouthSource = tbl["Mouth"]["Source"].value_or(s.mouthSource);
		s.mouthRule = tbl["Mouth"]["Rule"].value_or(s.mouthRule);
		s.climaxAnyMouth = tbl["Mouth"]["ClimaxAnyMouth"].value_or(s.climaxAnyMouth);
		s.poseLevel = std::clamp(tbl["Mouth"]["PoseLevel"].value_or(s.poseLevel), 0.05f, 1.0f);
		s.openThreshold = tbl["Mouth"]["OpenThreshold"].value_or(s.openThreshold);
		s.closeThreshold = tbl["Mouth"]["CloseThreshold"].value_or(s.closeThreshold);
		s.faceFollow = tbl["Mouth"]["FollowFaceLab"].value_or(s.faceFollow);
		s.faceOpen = tbl["Mouth"]["FaceLabOpen"].value_or(s.faceOpen);
		s.faceClose = tbl["Mouth"]["FaceLabClose"].value_or(s.faceClose);
		s.showDelay = tbl["Mouth"]["ShowDelay"].value_or(s.showDelay);
		s.hideDelay = tbl["Mouth"]["HideDelay"].value_or(s.hideDelay);
		s.slide = tbl["Slide"]["Enabled"].value_or(s.slide);
		s.slideOut = tbl["Slide"]["OutSeconds"].value_or(s.slideOut);
		s.slideIn = tbl["Slide"]["InSeconds"].value_or(s.slideIn);
		s.slideWait = tbl["Slide"]["WaitSeconds"].value_or(s.slideWait);
		s.slideDistance = tbl["Slide"]["Distance"].value_or(s.slideDistance);
		s.extendX = tbl["Slide"]["ExtendedX"].value_or(s.extendX);
		s.extendY = tbl["Slide"]["ExtendedY"].value_or(s.extendY);
		s.extendZ = tbl["Slide"]["ExtendedZ"].value_or(s.extendZ);
		s.askX = tbl["Slide"]["AskX"].value_or(s.askX);
		s.askY = tbl["Slide"]["AskY"].value_or(s.askY);
		s.askZ = tbl["Slide"]["AskZ"].value_or(s.askZ);
		s.askOver = tbl["Slide"]["AskOver"].value_or(s.askOver);
		s.visual = tbl["Visual"]["Enabled"].value_or(s.visual);
		s.visSize = std::clamp(tbl["Visual"]["Size"].value_or(s.visSize), -60.0f, 100.0f);
		s.visLength = std::clamp(tbl["Visual"]["Length"].value_or(s.visLength), -60.0f, 150.0f);
		s.visWidth = std::clamp(tbl["Visual"]["Width"].value_or(s.visWidth), -60.0f, 100.0f);
		s.visThickness = std::clamp(tbl["Visual"]["Thickness"].value_or(s.visThickness), -60.0f, 100.0f);
		s.visTilt = std::clamp(tbl["Visual"]["Tilt"].value_or(s.visTilt), -45.0f, 45.0f);
		s.slideX = tbl["Slide"]["OffsetX"].value_or(s.slideX);
		s.slideZ = tbl["Slide"]["OffsetZ"].value_or(s.slideZ);
		s.slideEase = tbl["Slide"]["Ease"].value_or(s.slideEase);
		s.hideDuringLines = tbl["Speech"]["HideDuringLines"].value_or(s.hideDuringLines);
		s.hideDuringMoans = tbl["Speech"]["HideDuringMoans"].value_or(s.hideDuringMoans);
		s.speechIn = tbl["Speech"]["InSeconds"].value_or(s.speechIn);
		s.speechOut = tbl["Speech"]["OutSeconds"].value_or(s.speechOut);
		s.speechGrace = tbl["Speech"]["Grace"].value_or(s.speechGrace);
		s.climaxMode = tbl["Climax"]["Mode"].value_or(s.climaxMode);
		s.climaxSeconds = tbl["Climax"]["Seconds"].value_or(s.climaxSeconds);
		s.player = tbl["Who"]["Player"].value_or(s.player);
		s.others = tbl["Who"]["Others"].value_or(s.others);
		s.females = tbl["Who"]["Females"].value_or(s.females);
		s.males = tbl["Who"]["Males"].value_or(s.males);
		g_s = s;
	}

	bool Save()
	{
		const auto&   s = g_s;
		std::ofstream o(Path(), std::ios::binary | std::ios::trunc);
		if (!o) {
			return false;
		}
		auto b = [](bool v) { return v ? "true" : "false"; };
		o << "# TongueLab. Everything here can be changed in the in-game menu (SKSE Menu Framework).\r\n\r\n"
		  << "[General]\r\nEnabled = " << b(s.enabled) << "\r\nDebug   = " << b(s.debug) << "   # log every tongue decision to TongueLab.log\r\n\r\n"
		  << "[Mouth]\r\n"
		  << "Source         = " << s.mouthSource << "   # 0 = mouth as drawn on screen, 1 = mouth as requested by OStim or a voice mod\r\n"
		  << "Rule           = " << s.mouthRule << "   # 0 = any open mouth, 1 = only OStim's tongue pose (BigAah + Eee): moans and talking do not bring the tongue out\r\n"
		  << "ClimaxAnyMouth = " << b(s.climaxAnyMouth) << "   # at climax any open mouth counts (ahegao expressions)\r\n"
		  << "PoseLevel      = " << s.poseLevel << "   # how big OStim's tongue pose is as drawn (BigAah/Eee). 1 = full size. Lower it if another mod makes OStim's tongue pose small\r\n"
		  << "OpenThreshold  = " << s.openThreshold << "   # the mouth must be this open (0-1) before the tongue comes out\r\n"
		  << "CloseThreshold = " << s.closeThreshold << "   # the tongue goes back in when the mouth falls below this\r\n"
		  << "FollowFaceLab  = " << b(s.faceFollow) << "   # when FaceLab draws the face, the tongue is only out while FaceLab's mouth is open\r\n"
		  << "FaceLabOpen    = " << s.faceOpen << "   # FaceLab mouth weight (1 = full) the tongue waits for before it comes out\r\n"
		  << "FaceLabClose   = " << s.faceClose << "   # the tongue goes in when FaceLab's mouth falls below this\r\n"
		  << "ShowDelay      = " << s.showDelay << "   # seconds the mouth must stay open before the tongue comes out\r\n"
		  << "HideDelay      = " << s.hideDelay << "   # seconds below the close threshold before the tongue goes in\r\n\r\n"
		  << "[Slide]\r\n"
		  << "Enabled    = " << b(s.slide) << "   # slide the tongue out of / back into the mouth (needs the Slide Bone tongue meshes)\r\n"
		  << "OutSeconds = " << s.slideOut << "\r\nInSeconds  = " << s.slideIn << "\r\nWaitSeconds = " << s.slideWait << "   # seconds to wait for the slide bone after OStim equips the tongue (0 = come out at once, without sliding if the bone is not there yet)\r\n"
		  << "Distance   = " << s.slideDistance << "   # how far back into the mouth the tongue goes\r\n"
		  << "ExtendedX  = " << s.extendX << "   # where the tongue sits when fully out: sideways\r\n"
		  << "ExtendedY  = " << s.extendY << "   # ... forward (+) / back (-)\r\n"
		  << "ExtendedZ  = " << s.extendZ << "   # ... up (+) / down (-)\r\n"
		  << "AskX       = " << s.askX << "   # for a tongue a FaceLab face asks for (TongueOut): added to the place above, sideways\r\n"
		  << "AskY       = " << s.askY << "   # ... forward (+) / back (-)\r\n"
		  << "AskZ       = " << s.askZ << "   # ... up (+) / down (-)\r\n"
		  << "AskOver    = " << s.askOver << "   # units further out per 1.0 of TongueOut above 1.0\r\n"
		  << "OffsetX    = " << s.slideX << "   # extra sideways offset while retracted\r\n"
		  << "OffsetZ    = " << s.slideZ << "   # extra up / down offset while retracted\r\n"
		  << "Ease       = " << s.slideEase << "   # 0 linear, 1 smooth, 2 fast then slow, 3 slow then fast\r\n\r\n"
		  << "[Visual]\r\n"
		  << "Enabled   = " << b(s.visual) << "   # change the shape of the tongue itself (whole-tongue size / length / width / thickness / tilt)\r\n"
		  << "Size      = " << s.visSize << "   # all directions together, percent change (0 = the mesh as shipped)\r\n"
		  << "Length    = " << s.visLength << "   # front to back, percent\r\n"
		  << "Width     = " << s.visWidth << "   # side to side, percent\r\n"
		  << "Thickness = " << s.visThickness << "   # up and down, percent\r\n"
		  << "Tilt      = " << s.visTilt << "   # tip up (+) / down (-), degrees\r\n"
		  << "\r\n"
		  << "[Speech]\r\n"
		  << "HideDuringLines = " << b(s.hideDuringLines) << "   # tongue stays in while a voice mod says a line (the voice mod has to report it, see the README)\r\n"
		  << "HideDuringMoans = " << b(s.hideDuringMoans) << "   # ...and while it plays a moan\r\n"
		  << "InSeconds         = " << s.speechIn << "   # how fast the tongue goes in when a line starts\r\n"
		  << "OutSeconds        = " << s.speechOut << "   # how slowly it comes out again after a line\r\n"
		  << "Grace             = " << s.speechGrace << "   # seconds the tongue stays in after the line ends\r\n\r\n"
		  << "[Climax]\r\n"
		  << "Mode    = " << s.climaxMode << "   # 0 = normal rules, 1 = tongue may stay out while speaking at climax (ahegao), 2 = tongue always in at climax\r\n"
		  << "Seconds = " << s.climaxSeconds << "   # how long after an orgasm counts as the climax\r\n\r\n"
		  << "[Who]\r\n"
		  << "Player  = " << b(s.player) << "\r\nOthers  = " << b(s.others) << "\r\nFemales = " << b(s.females) << "\r\nMales   = " << b(s.males) << "\r\n";
		return true;
	}
}
